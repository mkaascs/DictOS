typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#define NULL 0
#define VIDEO_BUF ((uint8_t*)0xB8000)

#define VIDEO_W 80
#define VIDEO_H 25

#define COL_DEFAULT 0x07
#define COL_GREEN 0x0A
#define COL_CYAN 0x0B
#define COL_RED 0x0C
#define COL_YELLOW 0x0E

#define MASK_ADDR ((uint8_t*)0x8000)
#define PIC1 0x20
#define KBD_DATA 0x60
#define KBD_STAT 0x64
#define CURSOR_PORT 0x3D4
#define IDT_INTR 0x8E
#define GDT_CS 0x08

#define SCAN_ENTER 28
#define SCAN_BS 14

#define CMD_MAX 40
#define MAX_MATCHES 512

#pragma pack(push, 1)
struct idt_entry {
    uint16_t base_lo;
    uint16_t segm_sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
};

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
};

#pragma pack(pop)
struct DictEntry {
    const char* en;
    const char* fi;
};

extern "C" void kmain();
__declspec(naked) void startup() {
    __asm { call kmain }
}

static __inline uint8_t inb(uint16_t port) {
    uint8_t data;
    __asm {
        push dx
        mov  dx, port
        in   al, dx
        mov  data, al
        pop  dx
    }
    return data;
}

static __inline void outb(uint16_t port, uint8_t data) {
    __asm {
        push dx
        mov  dx, port
        mov  al, data
        out  dx, al
        pop  dx
    }
}

static __inline void outw(uint16_t port, uint16_t data) {
    __asm {
        push dx
        mov  dx, port
        mov  ax, data
        out  dx, ax
        pop  dx
    }
}
static int k_strlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void k_itoa(int v, char* buf) {
    if (v == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    char tmp[12];
    int i = 0, neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static int g_cur_row = 0;
static int g_cur_col = 0;
static uint8_t g_color = COL_DEFAULT;
static void cursor_update() {
    uint16_t pos = (uint16_t)(g_cur_row * VIDEO_W + g_cur_col);
    outb(CURSOR_PORT, 0x0F);
    outb(CURSOR_PORT + 1, (uint8_t)(pos & 0xFF));
    outb(CURSOR_PORT, 0x0E);
    outb(CURSOR_PORT + 1, (uint8_t)((pos >> 8) & 0xFF));
}

static void video_clear() {
    uint8_t* p = VIDEO_BUF;
    for (int i = 0; i < VIDEO_W * VIDEO_H * 2; i += 2) {
        p[i] = ' ';
        p[i + 1] = COL_DEFAULT;
    }
    g_cur_row = 0;
    g_cur_col = 0;
    cursor_update();
}

static void video_scroll() {
    uint8_t* p = VIDEO_BUF;
    for (int r = 0; r < VIDEO_H - 1; r++) {
        for (int c = 0; c < VIDEO_W * 2; c++)
            p[r * VIDEO_W * 2 + c] = p[(r + 1) * VIDEO_W * 2 + c];
    }
    for (int c = 0; c < VIDEO_W * 2; c += 2) {
        p[(VIDEO_H - 1) * VIDEO_W * 2 + c] = ' ';
        p[(VIDEO_H - 1) * VIDEO_W * 2 + c + 1] = COL_DEFAULT;
    }
    g_cur_row = VIDEO_H - 1;
    g_cur_col = 0;
}

static void video_putchar(char c, uint8_t color) {
    if (c == '\n') {
        g_cur_col = 0;
        g_cur_row++;
        if (g_cur_row >= VIDEO_H) video_scroll();
        cursor_update();
        return;
    }
    if (c == '\r') {
        g_cur_col = 0;
        cursor_update();
        return;
    }
    uint8_t* p = VIDEO_BUF + (g_cur_row * VIDEO_W + g_cur_col) * 2;
    p[0] = (uint8_t)c;
    p[1] = color;
    g_cur_col++;
    if (g_cur_col >= VIDEO_W) {
        g_cur_col = 0;
        g_cur_row++;
        if (g_cur_row >= VIDEO_H) video_scroll();
    }
    cursor_update();
}

static void video_puts(const char* s, uint8_t color) {
    while (*s) video_putchar(*s++, color);
}

static void video_backspace() {
    if (g_cur_col > 0)
        g_cur_col--;
    else if (g_cur_row > 0) {
        g_cur_row--;
        g_cur_col = VIDEO_W - 1;
    }
    uint8_t* p = VIDEO_BUF + (g_cur_row * VIDEO_W + g_cur_col) * 2;
    p[0] = ' ';
    p[1] = COL_DEFAULT;
    cursor_update();
}

static struct idt_entry g_idt[256];
static struct idt_ptr g_idtp;
static void idt_register(int num, void* handler) {
    uint32_t addr = (uint32_t)handler;
    g_idt[num].base_lo = (uint16_t)(addr & 0xFFFF);
    g_idt[num].segm_sel = GDT_CS;
    g_idt[num].always0 = 0;
    g_idt[num].flags = IDT_INTR;
    g_idt[num].base_hi = (uint16_t)((addr >> 16) & 0xFFFF);
}

__declspec(naked) void default_intr_handler() {
    __asm pusha __asm mov al, 0x20 __asm out 0x20, al __asm popa __asm iretd
}

static void idt_init() {
    for (int i = 0; i < 256; i++) idt_register(i, default_intr_handler);
}

static void idt_load() {
    g_idtp.base = (uint32_t)(&g_idt[0]);
    g_idtp.limit = (uint16_t)(sizeof(struct idt_entry) * 256 - 1);
    __asm lidt g_idtp
}

static const char g_scan[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
    0,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 39,  '`', 0,   92,  'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-',
    '4', '5', '6', '+', '1', '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0};

#define KBD_BUF 64

static volatile char g_kbuf[KBD_BUF];
static volatile int g_khead = 0;
static volatile int g_ktail = 0;

static uint32_t g_rand_seed = 12345;
static void rand_tick()  { g_rand_seed += 1; }
static uint32_t rand_next() {
    g_rand_seed = g_rand_seed * 1664525 + 1013904223;
    return g_rand_seed;
}

static void kbd_push(char c) {
    int next = (g_khead + 1) % KBD_BUF;
    if (next != g_ktail) {
        g_kbuf[g_khead] = c;
        g_khead = next;
    }
}

static void kbd_process() {
    if (inb(KBD_STAT) & 0x01) {
        uint8_t sc = inb(KBD_DATA);
        if (sc == SCAN_ENTER)
            kbd_push('\n');
        else if (sc == SCAN_BS)
            kbd_push('\b');
        else if (sc < 128 && g_scan[sc])
            kbd_push(g_scan[sc]);
    }
    rand_tick();        
    outb(PIC1, 0x20);
}

__declspec(naked) void kbd_handler() {
    __asm pusha __asm call kbd_process __asm popa __asm iretd
}

static void kbd_init() {
    idt_register(0x09, kbd_handler);
    outb(PIC1 + 1, 0xFD);
}

static int kbd_has() { return g_khead != g_ktail; }
static char kbd_get() {
    while (!kbd_has()) __asm hlt 
        char c = g_kbuf[g_ktail];
    g_ktail = (g_ktail + 1) % KBD_BUF;
    return c;
}

static const struct DictEntry g_dict[] = {
{"above","yll\x84"},
{"address","osoite"},
{"afternoon","iltap\x84iv\x84"},
{"age","ik\x84"},
{"air","ilma"},
{"airport","lentokentt\x84"},
{"alone","yksin"},
{"already","jo"},
{"always","aina"},
{"angry","vihainen"},
{"animal","el\x84in"},
{"answer","vastaus"},
{"apple","omena"},
{"arm","k\x84sivarsi"},
{"army","armeija"},
{"art","taide"},
{"ask","kysym\x84"},
{"aunt","t\x84ti"},
{"autumn","syksy"},
{"baby","vauva"},
{"back","selk\x84"},
{"bag","laukku"},
{"ball","pallo"},
{"bank","pankki"},
{"bathroom","kylpyhuone"},
{"beach","ranta"},
{"beautiful","kaunis"},
{"bed","vuode"},
{"bedroom","makuuhuone"},
{"beef","naudanliha"},
{"beer","olut"},
{"begin","aloittaa"},
{"believe","uskoa"},
{"below","alla"},
{"belt","vy\x94"},
{"better","parempi"},
{"bicycle","polkupy\x94r\x84"},
{"big","iso"},
{"bird","lintu"},
{"black","musta"},
{"blue","sininen"},
{"book","kirja"},
{"bottle","pullo"},
{"box","laatikko"},
{"boy","poika"},
{"bread","leip\x84"},
{"bridge","silta"},
{"bright","kirkas"},
{"broken","rikki"},
{"brother","veli"},
{"building","rakennus"},
{"bus","bussi"},
{"butter","voi"},
{"button","nappi"},
{"buy","ostaa"},
{"cake","kakku"},
{"call","soittaa"},
{"camera","kamera"},
{"capital","p\x84\x84kaupunki"},
{"car","auto"},
{"card","kortti"},
{"careful","varovainen"},
{"cat","kissa"},
{"center","keskus"},
{"chair","tuoli"},
{"cheap","halpa"},
{"cheese","juusto"},
{"chicken","kana"},
{"child","lapsi"},
{"chocolate","suklaa"},
{"church","kirkko"},
{"circle","ympyr\x84"},
{"city","kaupunki"},
{"class","luokka"},
{"clean","puhdas"},
{"clever","\x84lyk\x84s"},
{"clock","kello"},
{"close","sulkea"},
{"coat","takki"},
{"coffee","kahvi"},
{"cold","kylm\x84"},
{"color","v\x84ri"},
{"come","tulla"},
{"computer","tietokone"},
{"corner","kulma"},
{"correct","oikea"},
{"cost","hinta"},
{"cotton","puuvilla"},
{"country","maa"},
{"cry","itke\x84"},
{"cup","kuppi"},
{"dangerous","vaarallinen"},
{"dark","tumma"},
{"daughter","tyt\x84r"},
{"day","p\x84iv\x84"},
{"dead","kuollut"},
{"deep","syv\x84"},
{"department","osasto"},
{"desert","aavikko"},
{"difference","ero"},
{"difficult","vaikea"},
{"dinner","illallinen"},
{"direction","suunta"},
{"dirty","likainen"},
{"distance","et\x84isyys"},
{"doctor","l\x84\x84k\x84ri"},
{"dog","koira"},
{"door","ovi"},
{"down","alas"},
{"draw","piirt\x84\x84"},
{"dream","uni"},
{"dress","mekko"},
{"drink","juoda"},
{"drive","ajaa"},
{"dry","kuiva"},
{"duck","ankka"},
{"early","aikainen"},
{"ear","korva"},
{"earth","maa"},
{"easy","helppo"},
{"eat","sy\x94\x84"},
{"egg","muna"},
{"electricity","s\x84hk\x94"},
{"empty","tyhjy\x84"},
{"end","loppu"},
{"enemy","vihollinen"},
{"engine","moottori"},
{"engineer","insin\x94\x94ri"},
{"enough","tarpeeksi"},
{"evening","ilta"},
{"event","tapahtuma"},
{"every","jokainen"},
{"example","esimerkki"},
{"expensive","kallis"},
{"experience","kokemus"},
{"explain","selitt\x84\x84"},
{"eye","silm\x84"},
{"face","kasvot"},
{"fact","tosiasia"},
{"factory","tehdas"},
{"fall","pudota"},
{"false","v\x84\x84r\x84"},
{"family","perhe"},
{"fast","nopea"},
{"fat","lihava"},
{"father","is\x84"},
{"feel","tuntea"},
{"festival","juhla"},
{"field","pelto"},
{"fill","t\x84ytt\x84\x84"},
{"find","l\x94yt\x84\x84"},
{"finger","sormi"},
{"first","ensimm\x84inen"},
{"fish","kala"},
{"flag","lippu"},
{"floor","lattia"},
{"flower","kukka"},
{"fly","lent\x84\x84"},
{"fog","sumu"},
{"follow","seurata"},
{"food","ruoka"},
{"foot","jalka"},
{"forest","mets\x84"},
{"foreign","ulkomainen"},
{"free","vapaa"},
{"fresh","tuore"},
{"friend","yst\x84v\x84"},
{"front","edess\x84"},
{"fruit","hedelm\x84"},
{"full","t\x84ynn\x84"},
{"future","tulevaisuus"},
{"garden","puutarha"},
{"gate","portti"},
{"girl","tytt\x94"},
{"give","antaa"},
{"glass","lasi"},
{"gloves","k\x84sineet"},
{"gold","kulta"},
{"good","hyv\x84"},
{"grandfather","isois\x84"},
{"grandmother","iso\x84iti"},
{"grass","ruoho"},
{"gray","harmaa"},
{"green","vihre\x84"},
{"grow","kasvaa"},
{"guest","vieras"},
{"guitar","kitara"},
{"half","puoli"},
{"hand","k\x84si"},
{"happy","onnellinen"},
{"harbor","satama"},
{"hard","kova"},
{"hat","hattu"},
{"head","p\x84\x84"},
{"health","terveys"},
{"heart","syd\x84n"},
{"heat","kuumuus"},
{"heavy","raskas"},
{"help","auttaa"},
{"here","t\x84\x84ll\x84"},
{"history","historia"},
{"hole","reik\x84"},
{"holiday","loma"},
{"home","koti"},
{"honey","hunaja"},
{"hope","toivo"},
{"horse","hevonen"},
{"hospital","sairaala"},
{"hotel","hotelli"},
{"hour","tunti"},
{"house","talo"},
{"hungry","n\x84lk\x84inen"},
{"husband","aviomies"},
{"ice","j\x84\x84"},
{"idea","idea"},
{"important","t\x84rke\x84"},
{"industry","teollisuus"},
{"inside","sis\x84ll\x84"},
{"island","saari"},
{"job","ty\x94"},
{"journey","matka"},
{"juice","mehu"},
{"jump","hyp\x84t\x84"},
{"key","avain"},
{"king","kuningas"},
{"kitchen","keitti\x94"},
{"knee","polvi"},
{"knife","veitsi"},
{"know","tiet\x84\x84"},
{"lake","j\x84rvi"},
{"land","maa"},
{"language","kieli"},
{"large","suuri"},
{"last","viimeinen"},
{"late","my\x94h\x84"},
{"laugh","nauraa"},
{"law","laki"},
{"learn","oppia"},
{"leather","nahka"},
{"leave","l\x84hte\x84"},
{"left","vasen"},
{"leg","jalka"},
{"less","v\x84hemm\x84n"},
{"letter","kirje"},
{"library","kirjasto"},
{"lie","valehdella"},
{"life","el\x84m\x84"},
{"light","valo"},
{"lion","leijona"},
{"listen","kuunnella"},
{"little","pieni"},
{"live","el\x84\x84"},
{"local","paikallinen"},
{"long","pitk\x84"},
{"look","katsoa"},
{"loud","kova"},
{"love","rakkaus"},
{"lunch","lounas"},
{"machine","kone"},
{"man","mies"},
{"map","kartta"},
{"market","tori"},
{"math","matematiikka"},
{"meal","ateria"},
{"meat","liha"},
{"medicine","l\x84\x84ke"},
{"meet","tavata"},
{"message","viesti"},
{"middle","keskell\x84"},
{"milk","maito"},
{"minute","minuutti"},
{"mirror","peili"},
{"mistake","virhe"},
{"modern","moderni"},
{"money","raha"},
{"moon","kuu"},
{"morning","aamu"},
{"mother","aiti"},
{"mountain","vuori"},
{"mouse","hiiri"},
{"movie","elokuva"},
{"music","musiikki"},
{"name","nimi"},
{"narrow","kapea"},
{"nature","luonto"},
{"necessary","tarpeellinen"},
{"neighbor","naapuri"},
{"news","uutiset"},
{"night","y\x94"},
{"north","pohjoinen"},
{"nose","nen\x84"},
{"nothing","ei mit\x84\x84n"},
{"number","numero"},
{"nurse","sairaanhoitaja"},
{"office","toimisto"},
{"oil","\x94ljy"},
{"old","vanha"},
{"open","avata"},
{"orange","oranssi"},
{"order","tilata"},
{"other","toinen"},
{"outside","ulkona"},
{"oven","uuni"},
{"page","sivu"},
{"paint","maalata"},
{"paper","paperi"},
{"park","puisto"},
{"passport","passi"},
{"peace","rauha"},
{"pen","kyn\x84"},
{"people","ihmiset"},
{"pepper","pippuri"},
{"percent","prosentti"},
{"phone","puhelin"},
{"photo","valokuva"},
{"piano","piano"},
{"picture","kuva"},
{"pink","vaaleanpunainen"},
{"place","paikka"},
{"plant","kasvi"},
{"play","leikki\x84"},
{"please","ole hyv\x84"},
{"pocket","tasku"},
{"police","poliisi"},
{"poor","k\x94yh\x84"},
{"port","satama"},
{"possible","mahdollinen"},
{"potato","peruna"},
{"power","voima"},
{"price","hinta"},
{"problem","ongelma"},
{"program","ohjelma"},
{"purple","violetti"},
{"push","ty\x94nt\x84\x84"},
{"queen","kuningatar"},
{"question","kysymys"},
{"quick","nopea"},
{"quiet","hiljainen"},
{"rain","sade"},
{"reach","saavuttaa"},
{"read","lukea"},
{"ready","valmis"},
{"reason","syy"},
{"receive","vastaanottaa"},
{"red","punainen"},
{"remember","muistaa"},
{"rent","vuokra"},
{"repair","korjata"},
{"rest","lepo"},
{"rich","rikas"},
{"right","oikea"},
{"river","joki"},
{"ring","sormus"},
{"road","tie"},
{"roof","katto"},
{"room","huone"},
{"rope","k\x94ysi"},
{"rough","karkea"},
{"round","py\x94re\x84"},
{"rule","s\x84\x84nt\x94"},
{"run","juosta"},
{"sad","surullinen"},
{"safe","turvallinen"},
{"salt","suola"},
{"same","sama"},
{"sand","hiekka"},
{"save","s\x84\x84st\x84\x84"},
{"school","koulu"},
{"sea","meri"},
{"second","toinen"},
{"see","n\x84hd\x84"},
{"send","l\x84hett\x84\x84"},
{"sharp","ter\x84v\x84"},
{"ship","laiva"},
{"shirt","paita"},
{"shoe","kenk\x84"},
{"shop","kauppa"},
{"short","lyhyt"},
{"sick","sairas"},
{"sign","merkki"},
{"silver","hopea"},
{"simple","yksinkertainen"},
{"sing","laulaa"},
{"sister","sisar"},
{"sit","istua"},
{"size","koko"},
{"skill","taito"},
{"sky","taivas"},
{"sleep","nukkua"},
{"slow","hidas"},
{"small","pieni"},
{"smart","\x84lyk\x84s"},
{"smile","hymy"},
{"snow","lumi"},
{"soft","pehme\x84"},
{"soldier","sotilas"},
{"son","poika"},
{"song","laulu"},
{"soup","keitto"},
{"south","etel\x84"},
{"speak","puhua"},
{"sport","urheilu"},
{"spring","kev\x84t"},
{"square","neli\x94"},
{"stand","seisoa"},
{"star","t\x84hti"},
{"station","asema"},
{"stay","j\x84\x84"},
{"step","askel"},
{"stone","kivi"},
{"stop","pys\x84hty\x84"},
{"store","varasto"},
{"story","tarina"},
{"straight","suora"},
{"strange","outo"},
{"street","katu"},
{"strong","vahva"},
{"student","opiskelija"},
{"subject","aihe"},
{"sugar","sokeri"},
{"summer","kes\x84"},
{"sun","aurinko"},
{"sweet","makea"},
{"swim","uida"},
{"table","p\x94yt\x84"},
{"tail","h\x84nt\x84"},
{"talk","puhua"},
{"tall","pitk\x84"},
{"taste","maku"},
{"tax","vero"},
{"teach","opettaa"},
{"teacher","opettaja"},
{"team","joukkue"},
{"telephone","puhelin"},
{"television","televisio"},
{"tell","kertoa"},
{"temperature","l\x84mp\x94tila"},
{"test","testi"},
{"thick","paksu"},
{"thin","ohut"},
{"think","ajatella"},
{"thirsty","janoinen"},
{"through","l\x84pi"},
{"ticket","lippu"},
{"time","aika"},
{"tired","v\x84synyt"},
{"today","t\x84n\x84\x84n"},
{"together","yhdess\x84"},
{"tomorrow","huomenna"},
{"tongue","kieli"},
{"tooth","hammas"},
{"touch","koskea"},
{"town","kaupunki"},
{"train","juna"},
{"travel","matkustaa"},
{"tree","puu"},
{"triangle","kolmio"},
{"true","totta"},
{"turn","k\x84\x84nty\x84"},
{"uncle","set\x84"},
{"under","alla"},
{"understand","ymm\x84rt\x84\x84"},
{"university","yliopisto"},
{"up","yl\x94s"},
{"use","k\x84ytt\x84\x84"},
{"vacation","loma"},
{"valley","laakso"},
{"vegetable","vihannes"},
{"village","kyl\x84"},
{"visit","vierailla"},
{"voice","\x84\x84ni"},
{"wait","odottaa"},
{"walk","k\x84vell\x84"},
{"wall","sein\x84"},
{"warm","l\x84mmin"},
{"wash","pest\x84"},
{"watch","katsoa"},
{"water","vesi"},
{"weak","heikko"},
{"wealth","varallisuus"},
{"weather","s\x84\x84"},
{"week","viikko"},
{"west","l\x84nsi"},
{"wet","m\x84rk\x84"},
{"white","valkoinen"},
{"wide","leve\x84"},
{"wife","vaimo"},
{"win","voittaa"},
{"wind","tuuli"},
{"window","ikkuna"},
{"wine","viini"},
{"winter","talvi"},
{"woman","nainen"},
{"wood","puu"},
{"word","sana"},
{"work","ty\x94"},
{"world","maailma"},
{"write","kirjoittaa"},
{"wrong","v\x84\x84r\x84"},
{"year","vuosi"},
{"yellow","keltainen"},
{"young","nuori"},
{"zero","nolla"},
{0,0}
};

static int g_dict_total = 0;
static void dict_init() {
    g_dict_total = 0;
    while (g_dict[g_dict_total].en != 0) g_dict_total++;
}

static const char* dict_find(const char* word) {
    int lo = 0, hi = g_dict_total - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = k_strcmp(word, g_dict[mid].en);
        if (cmp == 0) return g_dict[mid].fi;
        if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return NULL;
}

static int dict_count_letter(char letter) {
    int count = 0;
    for (int i = 0; i < g_dict_total; i++)
        if (g_dict[i].en[0] == letter) count++;
    return count;
}

static int dict_avail_letter(char letter) {
    uint8_t* mask = MASK_ADDR;
    if (mask[letter - 'a'] == 0) return 0;
    return dict_count_letter(letter);
}

static int dict_avail_total() {
    int total = 0;
    for (char c = 'a'; c <= 'z'; c++) total += dict_avail_letter(c);
    return total;
}

static void cmd_info() {
    video_puts("DictOS v1.0 en->fi Dictionary OS\n", g_color);
    video_puts("Developer: Maxim Atroschenko, SpbPU\n", g_color);
    video_puts("Bootloader: FASM\nKernel: MS C Compiler\n", g_color);
    video_puts("Bootloader param: letter filter = ", g_color);
    uint8_t* mask = MASK_ADDR;
    for (int i = 0; i < 26; i++) {
        if (mask[i]) {
            char ch[2];
            ch[0] = 'a' + i;
            ch[1] = 0;
            video_puts(ch, COL_YELLOW);
        }
    }
    video_puts("\n", g_color);
}

static void cmd_dictinfo() {
    char buf[16];
    video_puts("Dictionary: en -> fi\n", g_color);
    video_puts("Number of words: ", g_color);
    k_itoa(g_dict_total, buf);
    video_puts(buf, g_color);
    video_puts("\n", g_color);
    video_puts("Number of loaded words: ", g_color);
    k_itoa(dict_avail_total(), buf);
    video_puts(buf, g_color);
    video_puts("\n", g_color);
}

static void cmd_translate(const char* word) {
    if (!word || !word[0]) {
        video_puts("Usage: translate <word>\n", COL_RED);
        return;
    }
    char first = word[0];
    if (first >= 'a' && first <= 'z') {
        uint8_t* mask = MASK_ADDR;
        if (!mask[first - 'a']) {
            video_puts("Error: word '", COL_RED);
            video_puts(word, COL_RED);
            video_puts("' is not loaded (letter disabled)\n", COL_RED);
            return;
        }
    }
    const char* fi = dict_find(word);
    if (fi) {
        video_puts(fi, COL_YELLOW);
        video_puts("\n", g_color);
    } else {
        video_puts("Error: word '", COL_RED);
        video_puts(word, COL_RED);
        video_puts("' is unknown\n", COL_RED);
    }
}

static void cmd_wordstat(const char* arg) {
    if (!arg || !arg[0]) {
        video_puts("Usage: wordstat <letter>\n", COL_RED);
        return;
    }
    char c = arg[0];
    if (c < 'a' || c > 'z') {
        video_puts("Error: must be a-z\n", COL_RED);
        return;
    }
    char buf[16];
    video_puts("Letter '", g_color);
    char ls[2];
    ls[0] = c;
    ls[1] = 0;
    video_puts(ls, g_color);
    video_puts("': ", g_color);
    k_itoa(dict_avail_letter(c), buf);
    video_puts(buf, g_color);
    int total = dict_count_letter(c);
    k_itoa(total, buf);
    video_puts(" word(s) loaded (of ", g_color);
    video_puts(buf, g_color);
    video_puts(" total).\n", g_color);
}

static void cmd_shutdown() {
    video_puts("Powering off...\n", COL_YELLOW);
    outw(0x604, 0x2000);
    while (1) __asm hlt
}

static void cmd_anyword(const char* arg)
{
    char filter = 0; 
    if (arg && arg[0] >= 'a' && arg[0] <= 'z')
        filter = arg[0];
    else if (arg && arg[0] != 0) {
        video_puts("Error: argument must be a letter a-z\n", COL_RED);
        return;
    }

    int indices[MAX_MATCHES];
    int count = 0;
    uint8_t* mask = MASK_ADDR;

    for (int i = 0; i < g_dict_total; i++) {
        char first = g_dict[i].en[0];
        if (filter && first != filter) continue;       
        if (!mask[first - 'a']) continue;              
        indices[count++] = i;
    }

    if (count == 0) {
        video_puts("Error: no words", COL_RED);
        if (filter) {
            char ls[4]; ls[0] = ' '; ls[1] = '\''; ls[2] = filter; ls[3] = 0;
            video_puts(ls, COL_RED);
            video_puts("'", COL_RED);
        }
        video_puts("\n", COL_RED);
        return;
    }

    uint32_t r = rand_next() % (uint32_t)count;
    int idx = indices[r];

    video_puts(g_dict[idx].en, g_color);
    video_puts(": ", g_color);
    video_puts(g_dict[idx].fi, COL_YELLOW);
    video_puts("\n", g_color);
}

typedef void (*cmd_fn)(const char*);
static void cmd_info_wrap(const char*) { cmd_info(); }
static void cmd_dictinfo_wrap(const char*) { cmd_dictinfo(); }
static void cmd_shutdown_wrap(const char*) { cmd_shutdown(); }

struct Command {
    const char* name;
    cmd_fn execute;
};

static const struct Command g_commands[] = {
    {"info",      cmd_info_wrap},
    {"dictinfo",  cmd_dictinfo_wrap},
    {"translate", cmd_translate},
    {"wordstat",  cmd_wordstat},
    {"anyword",   cmd_anyword},
    {"shutdown",  cmd_shutdown_wrap},
    {0, 0}
};

static void skip_spaces(const char** p) {
    while (**p == ' ') (*p)++;
}

static void parse_and_run(char* cmd) {
    const char* p = cmd;
    skip_spaces(&p);
    if (!*p) return;
    const char* sp = p;
    while (*sp && *sp != ' ') sp++;
    char kw[16];
    int kwlen = (int)(sp - p);
    if (kwlen > 15) kwlen = 15;
    for (int i = 0; i < kwlen; i++) kw[i] = p[i];
    kw[kwlen] = 0;
    const char* arg = (*sp == ' ') ? sp + 1 : sp;
    skip_spaces(&arg);
    for (int i = 0; g_commands[i].name != 0; i++) {
        if (k_strcmp(kw, g_commands[i].name) == 0) {
            g_commands[i].execute(arg);
            return;
        }
    }

    video_puts("Error: command not recognized\n", COL_RED);
}

static char g_cmd[CMD_MAX + 1];
static int g_cmdlen = 0;
static void main_loop() {
    video_puts("# ", g_color);
    while (1) {
        char c = kbd_get();
        if (c == '\n') {
            g_cmd[g_cmdlen] = 0;
            video_puts("\n", g_color);
            if (g_cmdlen > 0) parse_and_run(g_cmd);
            g_cmdlen = 0;
            video_puts("# ", g_color);
        } else if (c == '\b') {
            if (g_cmdlen > 0) {
                g_cmdlen--;
                video_backspace();
            }
        } else {
            if (g_cmdlen < CMD_MAX) {
                g_cmd[g_cmdlen++] = c;
                video_putchar(c, g_color);
            }
        }
    }
}

extern "C" void kmain() {
    video_clear();
    idt_init();
    idt_load();
    kbd_init();
    __asm sti
    { int i = 0; while ((inb(KBD_STAT) & 0x01) && i < 256) { inb(KBD_DATA); i++; } }
    g_khead = 0;
    g_ktail = 0;
    dict_init();
    video_puts("Welcome to DictOS!\n", COL_YELLOW);
    video_puts("en->fi Dictionary OS\n", g_color);
    video_puts(
        "Commands: info, dictinfo, translate <word>, wordstat <letter>, anyword <letter>, "
        "shutdown\n",
        g_color);
    video_puts("\n", g_color);
    main_loop();
}
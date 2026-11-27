/* Learning Level Ordering Engine - Mastering C
 * Build : gcc -O2 -Wall -o engine engine.c
 * Run   : ./engine [--done 1,2,3] [--json | --static | --sql | --level N | --serve [port]]
 * Model : topics form a prerequisite DAG (ids are 1-based, 0 ends a list).
 *         Kahn's algorithm assigns levels and detects cycles; a greedy
 *         scheduler orders ready topics by (level, topics unlocked, difficulty). */
#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#define N 27
typedef unsigned long long u64;
typedef struct { const char *name, *track; int diff, hours, req[6]; } Topic;

static const Topic T[N] = {
 {"Hello World & Toolchain", "Foundations", 1, 2, {0}},
 {"Variables & Data Types",  "Foundations", 1, 3, {1}},
 {"Operators & Expressions", "Foundations", 1, 3, {2}},
 {"Input & Output",          "Foundations", 2, 3, {2}},
 {"Control Flow",            "Foundations", 2, 4, {3, 4}},
 {"Loops",                   "Foundations", 2, 4, {5}},
 {"Functions",               "Core",        3, 5, {6}},
 {"Arrays",                  "Core",        3, 5, {6}},
 {"Strings",                 "Core",        3, 5, {8}},
 {"Scope & Storage Classes", "Core",        3, 3, {7}},
 {"Bitwise Operations",      "Core",        4, 4, {3}},
 {"Pointers",                "Memory",      5, 8, {7, 8}},
 {"Pointer Arithmetic",      "Memory",      5, 5, {12}},
 {"Structs & Unions",        "Memory",      4, 5, {12}},
 {"Dynamic Memory",          "Memory",      6, 7, {12, 14}},
 {"Preprocessor & Macros",   "Craft",       3, 4, {7}},
 {"Multi-file Projects",     "Craft",       4, 4, {10, 16}},
 {"Build Systems & Make",    "Craft",       4, 3, {17}},
 {"File I/O",                "Craft",       4, 5, {9, 12}},
 {"Function Pointers",       "Advanced",    6, 5, {7, 12}},
 {"Linked Lists",            "Advanced",    6, 7, {14, 15}},
 {"Sorting & Searching",     "Advanced",    5, 6, {8, 20}},
 {"Trees, Stacks & Queues",  "Advanced",    7, 9, {21}},
 {"Debugging: gdb & Valgrind","Pro",        5, 5, {15}},
 {"Processes & Syscalls",    "Pro",         8, 10, {15, 19}},
 {"Concurrency with pthreads","Pro",        8, 8, {15, 20}},
 {"Capstone: Memory Allocator","Pro",       9, 15, {18, 23, 24, 25, 26}},
};

#define NL 12
static const struct { const char *title, *explain, *q, *a; } LV[NL] = {
 {"First Contact", "A C program is written as text, compiled into machine code, then run. You learn that edit, compile, run loop and the shape of main.",
  "A compiler is not one step. Name the four stages gcc runs from source file to executable.",
  "Preprocessing, compilation to assembly, assembling into an object file, then linking with libraries."},
 {"Data and Types", "Variables are named boxes with a fixed type. The type decides how many bytes the box uses and how those bytes are read.",
  "Why can unsigned u = 0; u - 1 give a huge number, while int i = INT_MAX; i + 1 is undefined behaviour?",
  "Unsigned arithmetic wraps around by definition. Signed overflow has no defined result, so the compiler may assume it never happens."},
 {"Expressions and I/O", "Operators combine values into new values, and printf and scanf move data out and in. Precedence decides which operator is applied first.",
  "What can int x = 5; int y = x++ + ++x; evaluate to, and why is writing this a bug?",
  "x is modified twice with no sequence point between the changes, so the behaviour is undefined and any result, even a crash, is allowed."},
 {"Decisions and Bits", "if and switch let a program choose a path. Bitwise operators work on the individual 0s and 1s inside a number.",
  "Why does the test n & (n - 1) == 0 not do what you expect for powers of two, and how do you write it correctly?",
  "== binds tighter than &, so it parses as n & ((n - 1) == 0). Write (n & (n - 1)) == 0 and also require n > 0, because 0 would pass."},
 {"Repetition", "Loops repeat work with for, while and do-while. The skill is picking the right loop and making sure it always ends.",
  "A countdown is written for (unsigned i = n; i >= 0; i--). What goes wrong and how do you fix it?",
  "An unsigned value is always >= 0, so the loop never ends and i wraps to a huge number. Use for (i = n; i-- > 0;) or a signed type."},
 {"Building Blocks", "Functions package reusable steps and arrays hold many values of one type in a row. C passes arguments by value, so a function receives copies.",
  "Inside main, sizeof(arr) gives the whole array size, but inside a function taking int arr[] it gives the size of a pointer. Why?",
  "An array parameter decays into a pointer to its first element, so the length is lost. Pass the element count as a separate argument."},
 {"Meeting Memory", "A pointer stores the address of another value. Strings are char arrays that end with a zero byte, and scope decides where a name can be seen.",
  "Why is changing a character through a char pointer to a string literal undefined, while the same edit on a char array initialised from that literal works?",
  "String literals usually sit in read-only memory. An array initialised from a literal gets its own writable copy."},
 {"Structured Memory", "Structs group related fields, pointer arithmetic steps through memory in units of a type, and files keep data between runs. Function pointers let you pass behaviour around.",
  "For struct { char a; int b; char c; } why is sizeof often 12 and not 6, and how do you shrink it to 8?",
  "The compiler inserts padding so the int sits on a 4-byte boundary and the total is a multiple of 4. Reorder the fields as int b, char a, char c."},
 {"Heap and Build", "malloc and free give you memory that outlives a function, and you must give every block back. Make rebuilds only what changed, and sorting shows how algorithm choice affects speed.",
  "If p = realloc(p, bigger) fails and returns NULL, what has leaked and what is the safe pattern?",
  "The old block is still allocated but you overwrote the only pointer to it. Assign to a temporary, check it for NULL, and only then update p."},
 {"Systems Craft", "Linked lists build structures from separate heap nodes. gdb and Valgrind hunt bugs, fork and exec start processes, and threads run code at the same time.",
  "Two threads each run counter++ one million times on a shared int. Why is the total usually below two million, and how do you fix it?",
  "counter++ is a read, add and write sequence, so two threads can read the same value and one update is lost. Protect it with a mutex or use an atomic increment."},
 {"Data Structures", "Stacks, queues and trees organise data for fast access. Pointers link the pieces together and recursion is a natural way to walk them.",
  "When you delete a binary search tree node that has two children, which node can take its place and why does the ordering stay valid?",
  "Use the in-order successor, the smallest node in the right subtree, or the in-order predecessor. It is greater than everything on the left and not greater than the rest of the right side."},
 {"Capstone", "Combine everything in one real project: a memory allocator that manages raw bytes, tracks free blocks and fights fragmentation.",
  "How can your allocator catch a double free, and why must returned addresses be aligned, for example to 16 bytes on x86-64?",
  "Keep a header with an in-use flag or magic value and check it in free. Alignment lets any type, including SSE vectors, be stored safely and efficiently at the returned address."},
};

static const char *TASK[NL] = {
 "Print exactly: Hello, World! followed by a newline.",
 "Read two ints a and b. Print their average with 2 decimals, e.g. 7 2 gives 4.50. Watch out for overflow.",
 "Read seconds s. Print H:MM:SS with zero-padded minutes and seconds, e.g. 3725 gives 1:02:05.",
 "Read an unsigned n. Print yes or no (is n a power of two) and its number of set bits, e.g. 12 gives no 2.",
 "Read n. Print how many Collatz steps (even: n/2, odd: 3n+1) it takes to reach 1, e.g. 6 gives 8.",
 "Read n then n ints. Print the second largest distinct value, or NONE. e.g. 5 then 3 1 4 1 5 gives 4.",
 "Read one line. Print its words in reverse order separated by single spaces, e.g. the quick fox gives fox quick the.",
 "Read n, then n lines of: name score. Print them sorted by score descending, then name ascending. Use a struct and qsort.",
 "Read n sorted ints, then q, then q queries. For each query print the index of its first occurrence or -1. Use binary search.",
 "Read commands until EOF: add x (insert at front), del x (remove first match), print (values separated by spaces, or EMPTY). Use a linked list.",
 "Read n then n ints and insert them into a binary search tree, skipping duplicates. Print the in-order values, then the tree height in nodes.",
 "Simulate a 100-byte heap with first-fit allocation (lowest address that fits). alloc id size prints the start address or FAIL. free id releases the block. Adjacent free space must merge.",
};
typedef struct { const char *in, *out; } Test;
static const Test TESTS[NL][3] = {
 {{"", "Hello, World!\n"}},
 {{"7 2\n", "4.50\n"}, {"-3 4\n", "0.50\n"}, {"2000000000 2000000000\n", "2000000000.00\n"}},
 {{"3725\n", "1:02:05\n"}, {"59\n", "0:00:59\n"}, {"86399\n", "23:59:59\n"}},
 {{"16\n", "yes 1\n"}, {"12\n", "no 2\n"}, {"0\n", "no 0\n"}},
 {{"6\n", "8\n"}, {"1\n", "0\n"}, {"27\n", "111\n"}},
 {{"5\n3 1 4 1 5\n", "4\n"}, {"3\n7 7 7\n", "NONE\n"}, {"2\n-1 -5\n", "-5\n"}},
 {{"the quick brown fox\n", "fox brown quick the\n"}, {"single\n", "single\n"}},
 {{"3\nbob 90\namy 90\ncat 95\n", "cat 95\namy 90\nbob 90\n"}, {"1\nzed 1\n", "zed 1\n"}},
 {{"5\n1 2 2 3 9\n3\n2 9 4\n", "1\n4\n-1\n"}, {"1\n5\n2\n5 6\n", "0\n-1\n"}},
 {{"add 1\nadd 2\nadd 3\ndel 2\nprint\n", "3 1\n"}, {"print\n", "EMPTY\n"}, {"add 5\ndel 7\nprint\n", "5\n"}},
 {{"5\n5 3 8 3 9\n", "3 5 8 9\n3\n"}, {"4\n1 2 3 4\n", "1 2 3 4\n4\n"}},
 {{"alloc a 40\nalloc b 40\nalloc c 40\nfree a\nalloc c 30\nfree b\nalloc d 50\n", "0\n40\nFAIL\n0\n30\n"}, {"alloc x 100\nalloc y 1\nfree x\nalloc y 1\n", "0\nFAIL\n0\n"}},
};
static const char *WRONG[NL][3] = {
 {"Lexing, parsing, optimising, then running it in an interpreter.", "Editing, saving, compiling, then debugging with gdb.", "Compiling, linking, loading, then garbage collecting."},
 {"Unsigned types use more bytes, so they hold bigger values than signed ones.", "Signed overflow always wraps to INT_MIN, while unsigned subtraction is undefined.", "Both wrap identically; the compiler only prints a warning for each."},
 {"It always gives 12, because ++ is evaluated before + from left to right.", "It gives 11, because the post-increment returns 5 and the pre-increment returns 6.", "It does not compile, because x cannot change twice in one statement."},
 {"& binds tighter than ==, so the test is fine except for negative n.", "It works, but && should replace & to avoid a compiler warning.", "It only works for signed types, so casting n to int fixes it."},
 {"The loop runs once and stops, because i-- makes i negative at once.", "The loop is fine; unsigned counters stop automatically at 0.", "It does not compile, because unsigned variables cannot be decremented."},
 {"sizeof only works inside main, because functions have their own compiler.", "The function gets a copy of the whole array, but sizeof reports a different unit.", "Array arguments are moved to the heap, where no size is known."},
 {"A char pointer holds one character, so writing a second one corrupts it.", "Arrays live on the heap and can be changed, but pointers live on the stack.", "Literals are fine to change; the compiler only warns about a missing const."},
 {"Each field is rounded up to 4 bytes; adding static removes the waste.", "sizeof adds hidden metadata to every struct; sorting the field names shrinks it.", "The compiler adds a hidden type tag; declaring the struct const removes it."},
 {"Nothing leaks, because realloc frees the old block even when it fails.", "The new block leaks; calling free(bigger) fixes it.", "The program always crashes at that line, so nothing can leak."},
 {"The compiler drops one of the loops; declaring counter register fixes it.", "Each thread gets its own private copy of counter by default.", "counter++ is atomic in C, so the total is always two million."},
 {"Use the root, because it always sits between the left and right subtrees.", "Use any leaf, then rotate the root until the tree is sorted again.", "Move the left child up and discard the right subtree."},
 {"Set the pointer to NULL inside free; alignment only matters on 8-bit machines.", "Call malloc again on the same address; the CPU aligns blocks automatically.", "Sort the free list after every free; alignment is only needed for strings."},
};
static int pos(int l) { return (l * 5 + 2) % 4; }   /* index of the correct option for level l (1-based) */

static int lvl[N], unl[N];   /* level (longest path), # of transitive dependents */
static u64 desc[N];          /* bitmask of all topics that depend on i */

static int analyse(void) {   /* Kahn's algorithm; returns 0 if a cycle exists */
    int indeg[N] = {0}, q[N], h = 0, t = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; T[i].req[j]; j++) indeg[i]++;
    for (int i = 0; i < N; i++) if (!indeg[i]) { lvl[i] = 1; q[t++] = i; }
    while (h < t) {
        int u = q[h++];
        for (int v = 0; v < N; v++)
            for (int j = 0; T[v].req[j]; j++)
                if (T[v].req[j] - 1 == u) {
                    if (lvl[v] < lvl[u] + 1) lvl[v] = lvl[u] + 1;
                    if (--indeg[v] == 0) q[t++] = v;
                }
    }
    for (int k = t - 1; k >= 0; k--)          /* reverse topological order */
        for (int v = 0; v < N; v++)
            for (int j = 0; T[v].req[j]; j++)
                if (T[v].req[j] - 1 == q[k]) desc[q[k]] |= desc[v] | (1ULL << v);
    for (int i = 0; i < N; i++) unl[i] = __builtin_popcountll(desc[i]);
    return t == N;
}

static int ready(int i, u64 done) {
    for (int j = 0; T[i].req[j]; j++)
        if (!((done >> (T[i].req[j] - 1)) & 1)) return 0;
    return 1;
}

static int better(int a, int b) {            /* priority among ready topics */
    if (lvl[a] != lvl[b]) return lvl[a] < lvl[b];
    if (unl[a] != unl[b]) return unl[a] > unl[b];
    if (T[a].diff != T[b].diff) return T[a].diff < T[b].diff;
    return a < b;
}

static int pick(u64 done) {
    int best = -1;
    for (int i = 0; i < N; i++)
        if (!((done >> i) & 1) && ready(i, done) && (best < 0 || better(i, best))) best = i;
    return best;
}

static int plan(u64 done, int *out) {        /* greedy schedule from current mastery */
    int n = 0, i;
    while ((i = pick(done)) >= 0) { out[n++] = i; done |= 1ULL << i; }
    return n;
}

static void show(u64 done) {
    int o[N], n = plan(done, o), hrs = 0;
    for (int k = 0; k < n; k++) hrs += T[o[k]].hours;
    printf("\nLEARNING LEVEL ORDERING ENGINE - Mastering C\nmastered %d/%d | %d topics, %dh to go\n\n",
           __builtin_popcountll(done), N, n, hrs);
    for (int k = 0; k < n; k++) {
        const Topic *t = &T[o[k]];
        printf("%s %2d. #%-2d L%-2d %-27s %-9.*s %2dh  unlocks %d\n", k ? "  " : "->", k + 1,
               o[k] + 1, lvl[o[k]], t->name, t->diff, "#########", t->hours, unl[o[k]]);
    }
}

static void show_level(int n) {
    if (n < 1 || n > NL) { fprintf(stderr, "level must be 1..%d\n", NL); return; }
    printf("\nLEVEL %d - %s\n%s\n\nTopics:", n, LV[n - 1].title, LV[n - 1].explain);
    for (int i = 0; i < N; i++) if (lvl[i] == n) printf(" [%s]", T[i].name);
    printf("\n\nHARD QUESTION\n%s\n\nCODING CHALLENGE\n%s\n", LV[n - 1].q, TASK[n - 1]);
}

static void fesc(FILE *o, const char *s) {      /* write s as the inside of a JS string */
    for (; *s; s++) {
        if (*s == '\n') fputs("\\n", o);
        else if (*s == '"' || *s == '\\') { fputc('\\', o); fputc(*s, o); }
        else fputc(*s, o);
    }
}

static void emit_json(FILE *out, int full) {
    int o[N], n = plan(0, o);
    fprintf(out, "const DATA={engine:\"C\",topics:[");
    for (int k = 0; k < n; k++) {
        const Topic *t = &T[o[k]];
        fprintf(out, "%s{id:%d,order:%d,name:\"%s\",track:\"%s\",level:%d,diff:%d,hours:%d,unlocks:%d,req:[",
               k ? "," : "", o[k] + 1, k + 1, t->name, t->track, lvl[o[k]], t->diff, t->hours, unl[o[k]]);
        for (int j = 0; t->req[j]; j++) fprintf(out, "%s%d", j ? "," : "", t->req[j]);
        fprintf(out, "]}");
    }
    fprintf(out, "],levels:[");
    for (int i = 0; i < NL; i++)
        {
        fprintf(out, "%s{title:\"%s\",explain:\"%s\",q:\"%s\",task:\"%s\",opts:[", i ? "," : "", LV[i].title, LV[i].explain, LV[i].q, TASK[i]);
        for (int k = 0, w = 0; k < 4; k++)
            fprintf(out, "%s\"%s\"", k ? "," : "", k == pos(i + 1) ? LV[i].a : WRONG[i][w++]);
        fprintf(out, "]");
        if (full) {                                   /* static hosting: answer + tests travel with the data */
            fprintf(out, ",ans:%d,tests:[", pos(i + 1));
            for (int k = 0; k < 3 && TESTS[i][k].in; k++) {
                fprintf(out, "%s{i:\"", k ? "," : "");
                fesc(out, TESTS[i][k].in);
                fprintf(out, "\",o:\"");
                fesc(out, TESTS[i][k].out);
                fprintf(out, "\"}");
            }
            fprintf(out, "]");
        }
        fprintf(out, "}");
    }
    fprintf(out, "]};\n");
}

static void emit_sql(void) {
    int o[N], n = plan(0, o);
    puts("CREATE TABLE topic(id INTEGER PRIMARY KEY, name TEXT, track TEXT, level INT, difficulty INT, hours INT, unlocks INT, ord INT);");
    puts("CREATE TABLE level(n INTEGER PRIMARY KEY, title TEXT, explanation TEXT, question TEXT, answer TEXT);");
    for (int i = 0; i < NL; i++)
        printf("INSERT INTO level VALUES(%d,'%s','%s','%s','%s');\n", i + 1, LV[i].title, LV[i].explain, LV[i].q, LV[i].a);
    puts("CREATE TABLE prereq(topic_id INT, requires_id INT);");
    puts("CREATE TABLE progress(topic_id INT PRIMARY KEY, mastered_at TEXT DEFAULT CURRENT_TIMESTAMP);");
    puts("CREATE VIEW next_up AS SELECT * FROM topic WHERE id NOT IN (SELECT topic_id FROM progress)"
         " AND NOT EXISTS (SELECT 1 FROM prereq p WHERE p.topic_id = topic.id AND p.requires_id NOT IN (SELECT topic_id FROM progress))"
         " ORDER BY level, unlocks DESC, difficulty;");
    for (int k = 0; k < n; k++) {
        const Topic *t = &T[o[k]];
        printf("INSERT INTO topic VALUES(%d,'%s','%s',%d,%d,%d,%d,%d);\n", o[k] + 1, t->name,
               t->track, lvl[o[k]], t->diff, t->hours, unl[o[k]], k + 1);
        for (int j = 0; t->req[j]; j++)
            printf("INSERT INTO prereq VALUES(%d,%d);\n", o[k] + 1, t->req[j]);
    }
}

/* ---- Judge + local web server (POSIX: Linux, macOS, WSL) ----
 * Compiles submitted code with gcc and runs it against hidden tests in a forked
 * child (stdin/stdout redirected, alarm() as the time limit). It EXECUTES the code
 * you submit, so it only listens on 127.0.0.1 and rejects foreign Host/Origin requests. */
static unsigned tmask, cmask;   /* levels whose theory / coding gate has been passed */
#define OPEN(l) ((l) == 1 || (((tmask & cmask) >> ((l) - 2)) & 1u))

static void save(void) {
    FILE *f = fopen("progress.txt", "w");
    if (f) { fprintf(f, "%u %u\n", tmask, cmask); fclose(f); }
}

static int same(const char *a, const char *b) {   /* equal ignoring trailing whitespace */
    size_t x = strlen(a), y = strlen(b);
    while (x && isspace((unsigned char)a[x - 1])) x--;
    while (y && isspace((unsigned char)b[y - 1])) y--;
    return x == y && !memcmp(a, b, x);
}

static int run(const char *in, const char *out, char *const av[], int secs) {
    pid_t p = fork();
    if (p == 0) {
        if (!freopen(in, "r", stdin) || !freopen(out, "w", stdout) || !freopen("/tmp/lle_err", "w", stderr)) _exit(126);
        for (int fd = 3; fd < 64; fd++) close(fd);   /* do not leak the server sockets */
        alarm(secs);
        execvp(av[0], av);
        _exit(127);
    }
    int st = 0;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;     /* -1: killed by a signal (crash/timeout) */
}

static int grade(int l, const char *code, char *msg, size_t cap) {
    FILE *f = fopen("/tmp/lle_src.c", "w");
    if (!f) { snprintf(msg, cap, "Server cannot write to /tmp."); return 0; }
    fputs(code, f); fclose(f);
    char *cc[] = {"gcc", "-O1", "-w", "-o", "/tmp/lle_bin", "/tmp/lle_src.c", NULL};
    if (run("/dev/null", "/dev/null", cc, 20)) {
        char b[2048] = "";
        if ((f = fopen("/tmp/lle_err", "r"))) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
        char *e = strstr(b, "error"), *nl;
        if (!e) e = b;
        if ((nl = strchr(e, '\n'))) *nl = 0;
        snprintf(msg, cap, "Compile error: %.200s", e);
        return 0;
    }
    char *ex[] = {"/tmp/lle_bin", NULL};
    int total = 0, pass = 0, first = 0;
    const char *why = "";
    for (int k = 0; k < 3 && TESTS[l - 1][k].in; k++) {
        total++;
        if ((f = fopen("/tmp/lle_in", "w"))) { fputs(TESTS[l - 1][k].in, f); fclose(f); }
        int rc = run("/tmp/lle_in", "/tmp/lle_out", ex, 2);
        char got[4096] = "";
        if ((f = fopen("/tmp/lle_out", "r"))) { size_t n = fread(got, 1, sizeof got - 1, f); got[n] = 0; fclose(f); }
        if (rc == 0 && same(got, TESTS[l - 1][k].out)) pass++;
        else if (!first) { first = total; why = rc ? "crash, timeout or non-zero exit" : "wrong output"; }
    }
    if (pass == total) snprintf(msg, cap, "All %d tests passed.", total);
    else snprintf(msg, cap, "%d/%d tests passed. First failure: test %d (%s).", pass, total, first, why);
    return pass == total;
}

static void jsay(int c, const char *s) {          /* write s as a JSON string body */
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') dprintf(c, "\\%c", *s);
        else dprintf(c, "%c", (unsigned char)*s < 32 ? ' ' : *s);
    }
}

static void serve(int port) {
    signal(SIGPIPE, SIG_IGN);
    FILE *pf = fopen("progress.txt", "r");
    if (pf) { if (fscanf(pf, "%u %u", &tmask, &cmask) != 2) tmask = cmask = 0; fclose(pf); }
    int s = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 8)) { perror("bind"); exit(1); }
    printf("Engine online (localhost only): http://localhost:%d  - Ctrl+C to stop\n", port);
    fflush(stdout);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        static char req[65536];
        int n = 0, need = 0, r;
        char *body = NULL;
        while (n < (int)sizeof req - 1 && (r = read(c, req + n, sizeof req - 1 - n)) > 0) {
            req[n += r] = 0;
            if (!body && (body = strstr(req, "\r\n\r\n"))) {
                body += 4;
                char *cl = strcasestr(req, "Content-Length:");
                need = cl ? atoi(cl + 15) : 0;
            }
            if (body && n - (int)(body - req) >= need) break;
        }
        if (!body) { close(c); continue; }
        body[-4] = 0;                                   /* req is now just the headers */
        char m[8] = "", p[256] = "";
        sscanf(req, "%7s %255s", m, p);
        int post = !strcmp(m, "POST");
        int l = strstr(p, "l=") ? atoi(strstr(p, "l=") + 2) : 0;
        int ch = strstr(p, "c=") ? atoi(strstr(p, "c=") + 2) : -1;
        const char *H = "HTTP/1.0 200 OK\r\nCache-Control: no-store\r\nContent-Type: ";
        if (!(strcasestr(req, "\nHost: localhost") || strcasestr(req, "\nHost: 127.0.0.1")) || (post && !strcasestr(req, "\nX-LLE:")))
            dprintf(c, "HTTP/1.0 403 Forbidden\r\n\r\nforbidden");
        else if (!strcmp(p, "/") || !strcmp(p, "/index.html")) {
            FILE *f = fopen("index.html", "r");
            char b[4096];
            size_t k;
            dprintf(c, "%stext/html; charset=utf-8\r\n\r\n", H);
            while (f && (k = fread(b, 1, sizeof b, f)) > 0 && write(c, b, k) > 0);
            if (f) fclose(f);
        } else if (!strcmp(p, "/data.js")) {
            dprintf(c, "%sapplication/javascript; charset=utf-8\r\n\r\n", H);
            FILE *o = fdopen(dup(c), "w");
            emit_json(o, 0);
            fclose(o);
        } else if (!strncmp(p, "/api/state", 10))
            dprintf(c, "%sapplication/json\r\n\r\n{\"theory\":%u,\"code\":%u}", H, tmask, cmask);
        else if (post && !strncmp(p, "/api/reset", 10)) {
            tmask = cmask = 0; save();
            dprintf(c, "%sapplication/json\r\n\r\n{}", H);
        } else if (post && !strncmp(p, "/api/quiz", 9) && l >= 1 && l <= NL && OPEN(l)) {
            int ok = ch == pos(l);
            if (ok) { tmask |= 1u << (l - 1); save(); }
            dprintf(c, "%sapplication/json\r\n\r\n{\"ok\":%s,\"answer\":\"%s\"}", H, ok ? "true" : "false", ok ? LV[l - 1].a : "");
        } else if (post && !strncmp(p, "/api/code", 9) && l >= 1 && l <= NL && OPEN(l)) {
            char msg[256] = "";
            int ok = grade(l, body, msg, sizeof msg);
            if (ok) { cmask |= 1u << (l - 1); save(); }
            dprintf(c, "%sapplication/json\r\n\r\n{\"ok\":%s,\"msg\":\"", H, ok ? "true" : "false");
            jsay(c, msg);
            dprintf(c, "\"}");
        } else dprintf(c, "HTTP/1.0 404 Not Found\r\n\r\nnot found");
        close(c);
    }
}

int main(int argc, char **argv) {
    if (!analyse()) { fputs("error: cycle in prerequisites\n", stderr); return 1; }
    u64 done = 0; int mode = 0, lv = 0, full = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--json")) mode = 1;
        else if (!strcmp(argv[a], "--static")) { mode = 1; full = 1; }
        else if (!strcmp(argv[a], "--sql")) mode = 2;
        else if (!strcmp(argv[a], "--level") && a + 1 < argc) { mode = 3; lv = atoi(argv[++a]); }
        else if (!strcmp(argv[a], "--serve")) { mode = 4; if (a + 1 < argc && atoi(argv[a + 1]) > 0) lv = atoi(argv[++a]); }
        else if (!strcmp(argv[a], "--done") && a + 1 < argc)
            for (char *s = strtok(argv[++a], ","); s; s = strtok(NULL, ",")) {
                int id = atoi(s);
                if (id >= 1 && id <= N) done |= 1ULL << (id - 1);
            }
    }
    if (mode == 1) emit_json(stdout, full); else if (mode == 2) emit_sql(); else if (mode == 3) show_level(lv); else if (mode == 4) serve(lv ? lv : 8080); else show(done);
    return 0;
}

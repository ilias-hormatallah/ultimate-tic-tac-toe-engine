#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <random>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <climits>
using namespace std;
using Clk = chrono::steady_clock;




static inline int popcnt(uint32_t x) {
    x -= (x >> 1) & 0x55555555u;
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    return (int)(((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u >> 24);
}



static const uint16_t WINS[8] = {0x007,0x038,0x1C0,0x049,0x092,0x124,0x111,0x054};
static const int SUB_W[9] = {4,3,4, 3,5,3, 4,3,4};
static const int CELL_W[9] = {3,2,3, 2,4,2, 3,2,3};
static const int SCORE_WIN = 1000000;
static const int SCORE_INF = 10000000;

static bool IS_WIN[512];
static uint8_t THREAT[512][512];
static int16_t EVAL_SUB[512*512];

static inline int sub_idx(uint16_t x, uint16_t o) { return (int)x*512+(int)o; }

static int count_two(uint16_t mine, uint16_t opp) {
    int n=0;
    for (auto w:WINS) if (!(opp&w) && popcnt(mine&w)==2) n++;
    return n;
}
static int count_one(uint16_t mine, uint16_t opp) {
    int n=0;
    for (auto w:WINS) if (!(opp&w) && popcnt(mine&w)==1) n++;
    return n;
}

static void init_tables() {
    for (int b=0;b<512;b++) {
        IS_WIN[b]=false;
        for (auto w:WINS) if ((b&w)==w){IS_WIN[b]=true;break;}
    }
    for (int x=0;x<512;x++) for (int o=0;o<512;o++) {
        THREAT[x][o]=0;
        if ((x&o)==0) {
            for (auto w:WINS) if (!(o&w)&&popcnt(x&w)==2) THREAT[x][o]++;
        }
    }
    for (int x=0;x<512;x++) for (int o=0;o<512;o++) {
        int16_t s=0;
        if ((x&o)==0) {
            if (IS_WIN[x]&&!IS_WIN[o]) s=600;
            else if (IS_WIN[o]&&!IS_WIN[x]) s=-600;
            else if ((x|o)==0x1FF) s=0;
            else {
                int tx=count_two(x,o), to_=count_two(o,x);
                int ox=count_one(x,o), oo=count_one(o,x);
                int cw=0;
                for (int c=0;c<9;c++){if(x&(1<<c))cw+=CELL_W[c];if(o&(1<<c))cw-=CELL_W[c];}
                s=(int16_t)(45*(tx-to_)+6*(ox-oo)+cw);
            }
        }
        EVAL_SUB[sub_idx(x,o)]=s;
    }
}


// ZOBRIST

static uint64_t Z_CELL[2][9][9], Z_SIDE, Z_CONSTR[11]; // index 0..9, +1 offset

static void init_zobrist() {
    mt19937_64 rng(0xDEADBEEFull);
    for (auto&a:Z_CELL) for (auto&b:a) for (auto&c:b) c=rng();
    Z_SIDE=rng();
    for (auto&z:Z_CONSTR) z=rng();
}


// STATE 

using Move = int; // sub*9 + cell
static inline int mv_sub(Move m){return m/9;}
static inline int mv_cell(Move m){return m%9;}
static inline int mv_col(Move m){return (mv_sub(m)%3)*3+(mv_cell(m)%3)+1;}
static inline int mv_row(Move m){return (mv_sub(m)/3)*3+(mv_cell(m)/3)+1;}
static Move mv_from_input(int col, int row){col--;row--;return ((row/3)*3+col/3)*9+((row%3)*3+col%3);}

struct State {
    uint16_t bx[9], bo[9];
    uint16_t meta_x, meta_o, meta_done;
    int constraint, current;
    uint64_t hash;
};

struct Undo {
    uint16_t bx, bo, meta_x, meta_o, meta_done;
    int constraint;
    uint64_t hash;
    int sub;
};

static int gen_moves(const State& st, Move* out) {
    int n=0;
    auto add=[&](int sub){
        uint16_t occ=st.bx[sub]|st.bo[sub];
        for (int c=0;c<9;c++) if (!(occ&(1<<c))) out[n++]=sub*9+c;
    };
    if (st.constraint>=0 && !((st.meta_done>>st.constraint)&1))
        add(st.constraint);
    else
        for (int i=0;i<9;i++) if (!((st.meta_done>>i)&1)) add(i);
    return n;
}

static void do_move(State& st, Move m, Undo& u) {
    int s=mv_sub(m), c=mv_cell(m);
    u.bx=st.bx[s]; u.bo=st.bo[s];
    u.meta_x=st.meta_x; u.meta_o=st.meta_o; u.meta_done=st.meta_done;
    u.constraint=st.constraint; u.hash=st.hash; u.sub=s;

    int p=st.current-1;
    if (p==0) st.bx[s]|=(uint16_t)(1u<<c); else st.bo[s]|=(uint16_t)(1u<<c);
    st.hash^=Z_CELL[p][s][c];

    if (!((st.meta_done>>s)&1)){
        uint16_t own=(p==0)?st.bx[s]:st.bo[s];
        if (IS_WIN[own]){
            if (p==0) st.meta_x|=(uint16_t)(1u<<s); else st.meta_o|=(uint16_t)(1u<<s);
            st.meta_done|=(uint16_t)(1u<<s);
        } else if ((st.bx[s]|st.bo[s])==0x1FF) st.meta_done|=(uint16_t)(1u<<s);
    }
    st.constraint=((st.meta_done>>c)&1)?-1:c;
    st.hash^=Z_CONSTR[u.constraint+1]^Z_CONSTR[st.constraint+1];
    st.current=3-st.current;
    st.hash^=Z_SIDE;
}

static void un_move(State& st, Move /*m*/, const Undo& u) {
    int s=u.sub;
    st.bx[s]=u.bx; st.bo[s]=u.bo;
    st.meta_x=u.meta_x; st.meta_o=u.meta_o; st.meta_done=u.meta_done;
    st.constraint=u.constraint; st.hash=u.hash;
    st.current=3-st.current;
}

static int terminal(const State& st) {
    if (IS_WIN[st.meta_x]) return 1;
    if (IS_WIN[st.meta_o]) return 2;
    if (st.meta_done==0x1FF){
        int xw=popcnt(st.meta_x),ow=popcnt(st.meta_o);
        return (xw>ow)?1:(ow>xw)?2:3;
    }
    return 0;
}


// EVALUATION


static int eval_leaf(const State& st) {
    int sc=0;
    for (int i=0;i<9;i++){
        if (!((st.meta_done>>i)&1))
            sc+=EVAL_SUB[sub_idx(st.bx[i],st.bo[i])]*SUB_W[i]/4;
    }
    for (int i=0;i<9;i++){
        if ((st.meta_x>>i)&1) sc+=120*SUB_W[i];
        if ((st.meta_o>>i)&1) sc-=120*SUB_W[i];
    }
    uint16_t mx=st.meta_x, mo=st.meta_o;
    uint16_t mdead=st.meta_done&~(mx|mo);
    int fx=0,fo=0;
    for (auto p:WINS){
        if (p&mdead) continue;
        int nx=popcnt(mx&p),no=popcnt(mo&p);
        if (nx&&!no){sc+=(nx==2)?2500:400;if(nx==2)fx++;}
        else if(no&&!nx){sc-=(no==2)?2500:400;if(no==2)fo++;}
    }
    if (fx>=2) sc+=3000;
    if (fo>=2) sc-=3000;
    if (st.constraint!=-1&&!((st.meta_done>>st.constraint)&1)){
        int s=st.constraint;
        uint16_t own=(st.current==1)?st.bx[s]:st.bo[s];
        uint16_t opp=(st.current==1)?st.bo[s]:st.bx[s];
        int bias=((int)THREAT[own&0x1FF][opp&0x1FF]-(int)THREAT[opp&0x1FF][own&0x1FF])*35;
        if (st.current==1) sc+=bias; else sc-=bias;
    } else {
        sc+=(st.current==1)?15:-15;
    }
    return sc;
}

static int evaluate(const State& st) {
    int t=terminal(st);
    if (t==1) return SCORE_WIN;
    if (t==2) return -SCORE_WIN;
    if (t==3) return (popcnt(st.meta_x)-popcnt(st.meta_o))*200;
    return eval_leaf(st);
}


// TT

enum { TT_EXACT=0, TT_LOWER=1, TT_UPPER=2 };
struct TTEntry {
    uint64_t key=0;
    int score=0;
    int16_t best=-1;
    int8_t depth=-1;
    uint8_t flag=0;
    bool valid=false;
};
static const int TT_SIZE=1<<21;
static TTEntry* TT;

static void tt_init(){TT=new TTEntry[TT_SIZE]();} 
static void tt_clear(){for(int i=0;i<TT_SIZE;i++)TT[i]=TTEntry();}
static TTEntry* tt_get(uint64_t k){return &TT[k&(TT_SIZE-1)];}
static void tt_store(uint64_t k,int sc,int d,int fl,int best){
    auto*e=tt_get(k);
    if (!e->valid||e->key!=k||d>=e->depth){
        e->key=k;e->score=sc;e->depth=d;e->flag=fl;e->best=best;e->valid=true;
    }
}




static Clk::time_point g_deadline;
static bool g_timeout;
static uint64_t g_nodes;
static const int MAX_PLY=64;
static Move g_killers[MAX_PLY][2];
static int g_history[2][81];

static inline bool time_check() {
    if ((g_nodes&4095)==0 && Clk::now()>=g_deadline) g_timeout=true;
    return g_timeout;
}


// MOVE ORDERING 

static int order_score(const State& st, Move m, Move tt_move, int ply) {
    if (m==tt_move) return 1000000;
    int s=mv_sub(m), c=mv_cell(m);
    int sc=0, p=st.current-1;
    uint16_t own=(p==0)?st.bx[s]:st.bo[s];
    uint16_t opp=(p==0)?st.bo[s]:st.bx[s];
    uint16_t newown=(uint16_t)(own|(1u<<c));

    if (IS_WIN[newown]){
        sc+=8000;
        uint16_t nm=(p==0)?(uint16_t)(st.meta_x|(1u<<s)):(uint16_t)(st.meta_o|(1u<<s));
        if (IS_WIN[nm]) sc+=500000;
    }

    for (auto pat:WINS){
        if ((pat&(1u<<c))&&(opp&pat)==(pat&(uint16_t)~(1u<<c))&&(own&pat)==0)
            {sc+=3500;break;}
    }
    if (ply<MAX_PLY){
        if (m==g_killers[ply][0]) sc+=2500;
        else if (m==g_killers[ply][1]) sc+=1500;
    }
    sc+=g_history[p][s*9+c];
    int ns=c;
    if (!((st.meta_done>>ns)&1)){
        uint16_t o_own=(p==0)?st.bo[ns]:st.bx[ns];
        uint16_t o_opp=(p==0)?st.bx[ns]:st.bo[ns];
        sc-=THREAT[o_own&0x1FF][o_opp&0x1FF]*120;
    } else sc-=60;
    sc+=CELL_W[c]*6+SUB_W[s]*3;
    return sc;
}

static void sort_moves(Move* mv, int* sc, int n) {
    for (int i=0;i<n-1;i++){
        int bi=i;
        for (int j=i+1;j<n;j++) if (sc[j]>sc[bi]) bi=j;
        if (bi!=i){swap(mv[i],mv[bi]);swap(sc[i],sc[bi]);}
    }
}


// ALPHA-BETA 

static int alphabeta(State& st, int depth, int alpha, int beta, int ply) {
    g_nodes++;
    if (time_check()) return 0;

    int t=terminal(st);
    if (t==1) return SCORE_WIN-ply;
    if (t==2) return -SCORE_WIN+ply;
    if (t==3) return (popcnt(st.meta_x)-popcnt(st.meta_o))*200;
    if (depth<=0) return eval_leaf(st);

    int a0=alpha, b0=beta;
    auto*e=tt_get(st.hash);
    Move tt_move=-1;
    if (e->valid&&e->key==st.hash){
        tt_move=e->best;
        if (e->depth>=depth){
            if (e->flag==TT_EXACT) return e->score;
            if (e->flag==TT_LOWER&&e->score>=beta) return e->score;
            if (e->flag==TT_UPPER&&e->score<=alpha) return e->score;
        }
    }

    Move moves[81]; int n=gen_moves(st,moves);
    if (n==0) return evaluate(st);

    int scores[81];
    for (int i=0;i<n;i++) scores[i]=order_score(st,moves[i],tt_move,ply);
    sort_moves(moves,scores,n);

    bool mx=(st.current==1);
    int best=mx?-SCORE_INF:SCORE_INF;
    Move bm=moves[0];

    for (int i=0;i<n;i++){
        Undo u;
        int v;
        int new_depth = depth - 1;


        bool reduced = false;
        if (i >= 3 && depth >= 3) {
            bool tactical = (moves[i]==tt_move) ||
                           (ply<MAX_PLY && (moves[i]==g_killers[ply][0]||moves[i]==g_killers[ply][1]));
            if (!tactical) {
                int s=mv_sub(moves[i]), c=mv_cell(moves[i]);
                uint16_t own=(st.current==1)?st.bx[s]:st.bo[s];
                if (!IS_WIN[(uint16_t)(own|(1u<<c))]) {
                    int R = 1 + (i >= 6) + (depth >= 6);  
                    new_depth = depth - 1 - R;
                    if (new_depth < 1) new_depth = 1;
                    reduced = true;
                }
            }
        }

        do_move(st,moves[i],u);


        if (i == 0) {

            v = alphabeta(st, new_depth, alpha, beta, ply+1);
        } else {
  
            if (mx)
                v = alphabeta(st, new_depth, alpha, alpha+1, ply+1);
            else
                v = alphabeta(st, new_depth, beta-1, beta, ply+1);


            if (!g_timeout) {
                if (mx && v > alpha && v < beta)
                    v = alphabeta(st, depth-1, alpha, beta, ply+1);  
                else if (!mx && v < beta && v > alpha)
                    v = alphabeta(st, depth-1, alpha, beta, ply+1);
            }
        }


        if (!g_timeout && reduced) {
            if (mx && v > alpha)
                v = alphabeta(st, depth-1, alpha, beta, ply+1);
            else if (!mx && v < beta)
                v = alphabeta(st, depth-1, alpha, beta, ply+1);
        }

        un_move(st,moves[i],u);
        if (g_timeout) return 0;

        if (mx){if(v>best){best=v;bm=moves[i];}if(best>alpha)alpha=best;}
        else{if(v<best){best=v;bm=moves[i];}if(best<beta)beta=best;}

        if (alpha>=beta){
            if (ply<MAX_PLY){
                if (g_killers[ply][0]!=moves[i]){
                    g_killers[ply][1]=g_killers[ply][0];
                    g_killers[ply][0]=moves[i];
                }
            }
            int p=st.current-1;
            g_history[p][mv_sub(moves[i])*9+mv_cell(moves[i])]+=depth*depth;
            if (g_history[p][mv_sub(moves[i])*9+mv_cell(moves[i])]>500000)
                for (int k=0;k<81;k++){g_history[0][k]/=2;g_history[1][k]/=2;}
            break;
        }
    }

    int fl=TT_EXACT;
    if (best<=a0) fl=TT_UPPER;
    else if (best>=b0) fl=TT_LOWER;
    tt_store(st.hash,best,depth,fl,bm);
    return best;
}


// ROOT SEARCH

static int search_root(State& st, int depth, Move& out) {
    auto*e=tt_get(st.hash);
    Move tt_move=(e->valid&&e->key==st.hash)?(Move)e->best:-1;

    Move moves[81]; int n=gen_moves(st,moves);
    if (n==0){out=-1;return 0;}

    int scores[81];
    for (int i=0;i<n;i++) scores[i]=order_score(st,moves[i],tt_move,0);
    sort_moves(moves,scores,n);

    bool mx=(st.current==1);
    int alpha=-SCORE_INF, beta=SCORE_INF;
    int best=mx?-SCORE_INF:SCORE_INF;
    Move bm=moves[0];

    for (int i=0;i<n;i++){
        Undo u;
        do_move(st,moves[i],u);
        int v;
        if (i == 0) {
            v = alphabeta(st, depth-1, alpha, beta, 1);
        } else {

            if (mx)
                v = alphabeta(st, depth-1, alpha, alpha+1, 1);
            else
                v = alphabeta(st, depth-1, beta-1, beta, 1);

            if (!g_timeout) {
                if (mx && v > alpha && v < beta)
                    v = alphabeta(st, depth-1, alpha, beta, 1);
                else if (!mx && v < beta && v > alpha)
                    v = alphabeta(st, depth-1, alpha, beta, 1);
            }
        }
        un_move(st,moves[i],u);
        if (g_timeout){out=bm;return best;}
        if (mx){if(v>best){best=v;bm=moves[i];}if(best>alpha)alpha=best;}
        else{if(v<best){best=v;bm=moves[i];}if(best<beta)beta=best;}
    }
    out=bm;
    tt_store(st.hash,best,depth,TT_EXACT,bm);
    return best;
}


// ITERATIVE DEEPENING

static Move find_best(State& st, int time_ms, bool verbose=false) {
    auto t0=Clk::now();
    g_deadline=t0+chrono::milliseconds(time_ms);
    g_timeout=false; g_nodes=0;
    memset(g_killers,0xFF,sizeof(g_killers));
    memset(g_history,0,sizeof(g_history));

    Move moves[81]; int n=gen_moves(st,moves);
    if (n==0) return -1;
    if (n==1) return moves[0];

    Move best=moves[0];
    for (int d=1;d<=40;d++){
        Move mv;
        int sc=search_root(st,d,mv);
        if (g_timeout) break;
        best=mv;

        if (sc>=SCORE_WIN-100||sc<=-SCORE_WIN+100) break;
        auto el=chrono::duration_cast<chrono::milliseconds>(Clk::now()-t0).count();
        if (verbose)
            cout<<"  d="<<d<<" sc="<<sc<<" n="<<g_nodes<<" "<<el<<"ms -> ("
                <<mv_col(best)<<","<<mv_row(best)<<")"<<endl;
        if (el*100>(long long)time_ms*65) break;
    }
    return best;
}


// SERIALIZATION

static uint64_t compute_hash(const State& st) {
    uint64_t h=0;
    for (int s=0;s<9;s++) for (int c=0;c<9;c++){
        if (st.bx[s]&(1<<c)) h^=Z_CELL[0][s][c];
        if (st.bo[s]&(1<<c)) h^=Z_CELL[1][s][c];
    }
    if (st.current==2) h^=Z_SIDE;
    h^=Z_CONSTR[st.constraint+1];
    return h;
}

static bool parse_state(const string& s, State& st) {
    istringstream is(s);
    int v[23];
    for (int i=0;i<23;i++) if (!(is>>v[i])) return false;
    for (int i=0;i<9;i++) st.bx[i]=(uint16_t)(v[i]&0x1FF);
    for (int i=0;i<9;i++) st.bo[i]=(uint16_t)(v[9+i]&0x1FF);
    st.meta_x=(uint16_t)(v[18]&0x1FF);
    st.meta_o=(uint16_t)(v[19]&0x1FF);
    st.meta_done=(uint16_t)(v[20]&0x1FF);
    st.constraint=v[21]; st.current=v[22];
    st.hash=compute_hash(st);
    return true;
}

[[maybe_unused]] static string serialize(const State& st) {
    ostringstream os;
    for (int i=0;i<9;i++) os<<st.bx[i]<<" ";
    for (int i=0;i<9;i++) os<<st.bo[i]<<" ";
    os<<st.meta_x<<" "<<st.meta_o<<" "<<st.meta_done<<" "<<st.constraint<<" "<<st.current;
    return os.str();
}


// DISPLAY

static void display(const State& st) {
    cout<<endl<<"     1   2   3   4   5   6   7   8   9"<<endl;
    cout<<"   +---+---+---+---+---+---+---+---+---+"<<endl;
    for (int row=0;row<9;row++){
        int sr=row/3,cr=row%3;
        cout<<" "<<(row+1)<<" |";
        for (int col=0;col<9;col++){
            int sc=col/3,cc=col%3;
            int sub=sr*3+sc,cell=cr*3+cc;
            char ch='.';
            if (st.bx[sub]&(1<<cell)) ch='X';
            else if (st.bo[sub]&(1<<cell)) ch='O';
            bool play=(ch=='.'&&!((st.meta_done>>sub)&1)&&(st.constraint<0||st.constraint==sub));
            if (play) cout<<" * "; else cout<<" "<<ch<<" ";
            if (cc==2&&col<8) cout<<"|"; else if (col<8) cout<<" ";
        }
        cout<<"|"<<endl;
        if (cr==2&&row<8) cout<<"   +---+---+---+---+---+---+---+---+---+"<<endl;
    }
    cout<<"   +---+---+---+---+---+---+---+---+---+"<<endl;
    cout<<"\n   Turn: "<<(st.current==1?"X":"O");
    if (st.constraint>=0) cout<<"  Zone "<<st.constraint;
    else cout<<"  Free";
    cout<<"  Meta: ";
    for (int i=0;i<9;i++){
        if ((st.meta_x>>i)&1) cout<<"X"; else if ((st.meta_o>>i)&1) cout<<"O";
        else if ((st.meta_done>>i)&1) cout<<"="; else cout<<".";
        if (i%3==2&&i<8) cout<<"/";
    }
    cout<<endl;
}


// CLI / PIPE / INTERACTIVE

int main(int argc, char* argv[]) {
    init_tables();
    init_zobrist();
    tt_init();

    if (argc>=2 && string(argv[1])=="cli") {
        if (argc<4){cerr<<"Usage: ./uttt cli \"state\" time_ms"<<endl;return 1;}
        State st; parse_state(argv[2],st);
        int tms=atoi(argv[3]);
        Move m=find_best(st,tms);
        cout<<mv_col(m)<<" "<<mv_row(m)<<endl;
        return 0;
    }

    if (argc>=3 && string(argv[1])=="pipe") {
        int tms=atoi(argv[2]);
        string line;
        while (getline(cin,line)){
            if (line=="quit"||line=="q") break;
            State st; 
            if (!parse_state(line,st)) continue;
            Move m=find_best(st,tms);
            cout<<mv_col(m)<<" "<<mv_row(m)<<endl;
            cout.flush();
        }
        return 0;
    }


    cout<<"ULTIMATE TIC TAC TOE"<<endl;
    cout<<"1=Human vs AI  2=Quit"<<endl;
    int ch; cin>>ch;
    if (ch==1){
        cout<<"Play as (1=X/2=O): ";int side;cin>>side;
        if (side!=1&&side!=2){cout<<"Invalid choice."<<endl;return 1;}
        cout<<"AI time ms: ";int tms;cin>>tms;
        if (tms<100){cout<<"Minimum is 100 ms."<<endl;tms=100;}
        State st={}; st.constraint=-1; st.current=1; st.hash=compute_hash(st);
        while (true){
            display(st);
            int r=terminal(st);
            if (r){cout<<(r==1?"X":r==2?"O":"Draw")<<" WINS!"<<endl;break;}
            if (st.current==side){
                int c,ro;
                cout<<"Move (col row): ";
                if (!(cin>>c>>ro)) {
                    cin.clear(); cin.ignore(10000,'\n');
                    cout<<"  Error: enter two numbers."<<endl;
                    continue;
                }
                if (c<1||c>9||ro<1||ro>9){
                    cout<<"  Out of range: col and row must be between 1 and 9."<<endl;
                    continue;
                }
                Move m=mv_from_input(c,ro);

                Move legal[81]; int nl=gen_moves(st,legal);
                bool ok=false;
                for (int i=0;i<nl;i++) if (legal[i]==m){ok=true;break;}
                if (!ok){
                    cout<<"  Illegal move! ";
                    if (st.constraint>=0){
                        int sr=st.constraint/3,sc2=st.constraint%3;
                        cout<<"You must play in zone ("
                            <<(sc2*3+1)<<"-"<<(sc2*3+3)<<","<<(sr*3+1)<<"-"<<(sr*3+3)<<").";
                    } else cout<<"That cell is occupied or its sub-board is already finished.";
                    cout<<endl;
                    continue;
                }
                Undo u; do_move(st,m,u);
                cout<<"  You: ("<<c<<","<<ro<<")"<<endl;
            } else {
                tt_clear();
                Move m=find_best(st,tms,true);
                Undo u; do_move(st,m,u);
                cout<<"AI: ("<<mv_col(m)<<","<<mv_row(m)<<")"<<endl;
            }
        }
    }
    return 0;
}

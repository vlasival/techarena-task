// TechArena: self-contained C++20 solver, no external dependencies.
// Reads instance.json and repeatedly writes the best feasible solution.json.

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <numeric>
#include <random>
#include <queue>
#include <unordered_map>
#include <limits>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct SearchTimeout {};
struct Budget {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    std::clock_t cpuStart=std::clock();
    double limit;
    unsigned ticks=0;
    explicit Budget(double seconds):limit(std::clamp(seconds,0.001,270.0)){}
    double elapsed()const{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
    void check()const{
        if(elapsed()>=limit||double(std::clock()-cpuStart)/CLOCKS_PER_SEC>=limit)throw SearchTimeout{};
    }
    void poll(){if((++ticks&255)==0)check();}
};
Budget* activeBudget=nullptr;

std::string read_file(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		throw std::runtime_error("cannot open input");
	}
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void skip_ws(const std::string& s, std::size_t& i) {
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
		++i;
	}
}

struct Json {
	enum class Type { Null, Bool, Number, String, Array, Object };
	Type type = Type::Null;
	double number = 0;
	std::string str;
	std::vector<Json> array;
	std::vector<std::pair<std::string, Json>> object;

	static Json parse(const std::string& s, std::size_t& i);

	static Json parse_value(const std::string& s, std::size_t& i) {
		if(activeBudget)activeBudget->poll();
		skip_ws(s, i);
		if (i >= s.size()) {
			throw std::runtime_error("unexpected end");
		}
		const char c = s[i];
		if (c == '{') {
			return parse_object(s, i);
		}
		if (c == '[') {
			return parse_array(s, i);
		}
		if (c == '"') {
			return parse_string(s, i);
		}
		if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
			return parse_number(s, i);
		}
		throw std::runtime_error("bad json");
	}

	static Json parse_object(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Object;
		++i;
		skip_ws(s, i);
		if (i < s.size() && s[i] == '}') {
			++i;
			return out;
		}
		while (true) {
			skip_ws(s, i);
			Json key = parse_string(s, i);
			skip_ws(s, i);
			if (i >= s.size() || s[i] != ':') {
				throw std::runtime_error("expected :");
			}
			++i;
			out.object.emplace_back(key.str, parse_value(s, i));
			skip_ws(s, i);
			if (i >= s.size()) {
				throw std::runtime_error("unclosed object");
			}
			if (s[i] == '}') {
				++i;
				return out;
			}
			if (s[i] != ',') {
				throw std::runtime_error("expected ,");
			}
			++i;
		}
	}

	static Json parse_array(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Array;
		++i;
		skip_ws(s, i);
		if (i < s.size() && s[i] == ']') {
			++i;
			return out;
		}
		while (true) {
			out.array.push_back(parse_value(s, i));
			skip_ws(s, i);
			if (i >= s.size()) {
				throw std::runtime_error("unclosed array");
			}
			if (s[i] == ']') {
				++i;
				return out;
			}
			if (s[i] != ',') {
				throw std::runtime_error("expected ,");
			}
			++i;
		}
	}

	static Json parse_string(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::String;
		if (s[i] != '"') {
			throw std::runtime_error("expected string");
		}
		++i;
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') {
				return out;
			}
			if (c == '\\' && i < s.size()) {
				out.str.push_back(s[i++]);
			} else {
				out.str.push_back(c);
			}
		}
		throw std::runtime_error("unterminated string");
	}

	static Json parse_number(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Number;
		const std::size_t start = i;
		if (s[i] == '-') {
			++i;
		}
		while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' ||
		                        s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
			++i;
		}
		out.number = std::stod(s.substr(start, i - start));
		return out;
	}

	const Json* find(const std::string& key) const {
		if (type != Type::Object) {
			return nullptr;
		}
		for (const auto& entry : object) {
			if (entry.first == key) {
				return &entry.second;
			}
		}
		return nullptr;
	}
};

Json Json::parse(const std::string& s, std::size_t& i) {
	return parse_value(s, i);
}

using U64 = std::uint64_t;
struct Point { double x,y; };
struct Site { double reward; int tasks; U64 need; };
struct Office { double cost; int cap; };
struct Group {
    std::vector<int> sites, hull;
    std::vector<U64> inside,owned;
    double xmin=0,xmax=0,ymin=0,ymax=0;
    U64 need=0;
    int load=0;
    double gain=0;
    bool active=false;
};
struct State {
    std::vector<Group> g;
    std::vector<int> owner;
    double raw=0,score=0;
    int clashes=0;
};

// Range additions; maximize value over leaves with the fewest missing offices.
struct IntervalTree {
    struct Node {int missing=0,lazyMissing=0,arg=0;double value=0,lazyValue=0;};
    int size;
    std::vector<Node> tree;
    IntervalTree(int n,int missing,double alpha):size(n),tree(4*n){init(1,0,n-1,missing,alpha);}
    void init(int p,int l,int r,int missing,double alpha){
        tree[p].missing=missing;tree[p].arg=r;tree[p].value=alpha*r;
        if(l<r){int c=(l+r)/2;init(p*2,l,c,missing,alpha);init(p*2+1,c+1,r,missing,alpha);}
    }
    void apply(int p,int missing,double value){tree[p].missing+=missing;tree[p].lazyMissing+=missing;tree[p].value+=value;tree[p].lazyValue+=value;}
    void push(int p){if(tree[p].lazyMissing||tree[p].lazyValue){apply(p*2,tree[p].lazyMissing,tree[p].lazyValue);apply(p*2+1,tree[p].lazyMissing,tree[p].lazyValue);tree[p].lazyMissing=0;tree[p].lazyValue=0;}}
    void pull(int p){const auto& a=tree[p*2];const auto& b=tree[p*2+1];const auto& v=a.missing<b.missing||(a.missing==b.missing&&a.value>b.value)?a:b;tree[p].missing=v.missing;tree[p].value=v.value;tree[p].arg=v.arg;}
    void add(int lo,int hi,int missing,double value){add(1,0,size-1,lo,hi,missing,value);}
    void add(int p,int l,int r,int lo,int hi,int missing,double value){
        if(lo<=l&&r<=hi){apply(p,missing,value);return;}push(p);int c=(l+r)/2;
        if(lo<=c)add(p*2,l,c,lo,hi,missing,value);if(hi>c)add(p*2+1,c+1,r,lo,hi,missing,value);pull(p);
    }
};

struct Solver {
    int n,m,w,K,P,B,D;
    double alpha,unit=1,countPenalty;
    std::vector<Point> points;
    std::vector<Site> sites;
    std::vector<Office> offices;
    std::vector<std::vector<double>> profit;
    std::vector<U64> workers;
    std::vector<int> supply;
    std::vector<double> facilityValue;
    std::vector<std::vector<int>> officePool,siteOffices,neighbors;
    bool singleton=true;
    std::mt19937_64 rng;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    double seconds=285;
    std::unordered_map<U64,std::vector<U64>> edges;
    State best;
    std::vector<State> population;
    double bestScore=0;
    std::string output;
    long long iterations=0;
    bool softGeometry=false;
    double geometryPrice=0;
    Budget& budget;

    double elapsed() const { return budget.elapsed(); }
    int ri(int a) { return int(rng()%a); }
    double rf() { return (rng()>>11)*0x1.0p-53; }
    static double cross(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
    Solver(const Json& root, double sec, U64 seed, std::string out,Budget& timer): rng(seed),seconds(sec),output(std::move(out)),budget(timer) {
        auto& sj=root.find("sites")->array;
        auto& oj=root.find("offices")->array;
        auto& wj=root.find("workers")->array;
        n=int(sj.size());m=int(oj.size());w=int(wj.size());P=n+m;B=(P+63)/64;
        D=int(root.find("maxDifference")->number);alpha=root.find("balanceCoefficient")->number;
        std::vector<int> skills;
        for(auto& a:wj)for(auto& t:a.array)skills.push_back(int(t.number));
        for(auto& a:sj)for(auto& t:a.find("tasks")->array)skills.push_back(int(t.number));
        std::sort(skills.begin(),skills.end());skills.erase(std::unique(skills.begin(),skills.end()),skills.end());
        K=int(skills.size());
        if(K>64)throw std::runtime_error("more than 64 skill types");
        auto mask=[&](const Json& a){U64 v=0;for(auto& t:a.array)v|=U64(1)<<int(std::lower_bound(skills.begin(),skills.end(),int(t.number))-skills.begin());return v;};
        supply.assign(K,0);
        for(auto& a:wj){U64 v=mask(a);workers.push_back(v);if(std::popcount(v)>1)singleton=false;for(int k=0;k<K;k++)if(v>>k&1)supply[k]++;}
        for(auto& a:sj){points.push_back({a.find("x")->number,a.find("y")->number});sites.push_back({a.find("reward")->number,int(a.find("tasks")->array.size()),mask(*a.find("tasks"))});}
        for(auto& a:oj){points.push_back({a.find("x")->number,a.find("y")->number});offices.push_back({a.find("cost")->number,int(a.find("maxSize")->number)});}
        profit.assign(m,std::vector<double>(n));double sum=0;int cnt=0;
        auto& dj=root.find("distances")->array;
        for(int o=0;o<m;o++){budget.check();for(int s=0;s<n;s++){profit[o][s]=sites[s].reward-dj[o].array[s].number;if(profit[o][s]>0){sum+=profit[o][s];cnt++;}}}
        unit=cnt?sum/cnt:1;countPenalty=unit*5+alpha;
        facilityValue.assign(m,0);
        best=empty();
    }
    State empty() { State s;s.g.resize(m);s.owner.assign(n,-1);return s; }
    void prepareCandidates(){
        officePool.resize(m);siteOffices.resize(n);neighbors.resize(n);
        for(int o=0;o<m;o++){
            budget.check();auto& v=officePool[o];v.resize(n);std::iota(v.begin(),v.end(),0);
            std::sort(v.begin(),v.end(),[&](int a,int b){return profit[o][a]>profit[o][b];});
        }
        for(int x=0;x<n;x++){
            budget.poll();std::vector<int> v(m);std::iota(v.begin(),v.end(),0);
            int take=std::min(m,8);std::partial_sort(v.begin(),v.begin()+take,v.end(),[&](int a,int b){return profit[a][x]>profit[b][x];});
            siteOffices[x].assign(v.begin(),v.begin()+take);
            auto distance=[&](int o){double dx=points[x].x-points[n+o].x,dy=points[x].y-points[n+o].y;return dx*dx+dy*dy;};
            take=std::min(m,6);std::partial_sort(v.begin(),v.begin()+take,v.end(),[&](int a,int b){return distance(a)<distance(b);});
            for(int j=0;j<take;j++)if(std::find(siteOffices[x].begin(),siteOffices[x].end(),v[j])==siteOffices[x].end())siteOffices[x].push_back(v[j]);
        }
        struct Node{int point,left=-1,right=-1,axis;};std::vector<Node> nodes;nodes.reserve(n);
        std::vector<int> order(n);std::iota(order.begin(),order.end(),0);
        auto coordinate=[&](int p,int axis){return axis?points[p].y:points[p].x;};
        auto buildTree=[&](auto&& self,int l,int r,int axis)->int{
            if(l>=r)return -1;budget.poll();int mid=(l+r)/2;
            std::nth_element(order.begin()+l,order.begin()+mid,order.begin()+r,[&](int a,int b){double aa=coordinate(a,axis),bb=coordinate(b,axis);return aa!=bb?aa<bb:a<b;});
            int id=int(nodes.size());nodes.push_back({order[mid],-1,-1,axis});
            int left=self(self,l,mid,1-axis),right=self(self,mid+1,r,1-axis);nodes[id].left=left;nodes[id].right=right;return id;
        };
        int root=buildTree(buildTree,0,n,0),take=std::min(24,n-1);
        if(take<=0)return;
        for(int x=0;x<n;x++){
            budget.poll();std::priority_queue<std::pair<double,int>> heap;
            auto query=[&](auto&& self,int node)->void{
                if(node<0)return;auto& nd=nodes[node];int p=nd.point;
                double dx=points[x].x-points[p].x,dy=points[x].y-points[p].y,d=dx*dx+dy*dy;
                if(p!=x&&(int(heap.size())<take||d<heap.top().first)){heap.push({d,p});if(int(heap.size())>take)heap.pop();}
                double diff=coordinate(x,nd.axis)-coordinate(p,nd.axis);
                self(self,diff<0?nd.left:nd.right);
                if(int(heap.size())<take||diff*diff<heap.top().first)self(self,diff<0?nd.right:nd.left);
            };
            query(query,root);while(!heap.empty()){neighbors[x].push_back(heap.top().second);heap.pop();}
        }
    }
    int candidate(int o){
        int take=std::min(n,std::max(32,offices[o].cap*3));
        return officePool[o][std::min(take-1,int(rf()*rf()*take))];
    }
    std::vector<int> hull(std::vector<int> v) {
        budget.poll();
        std::sort(v.begin(),v.end(),[&](int a,int b){return points[a].x!=points[b].x?points[a].x<points[b].x:points[a].y<points[b].y;});
        v.erase(std::unique(v.begin(),v.end(),[&](int a,int b){return points[a].x==points[b].x&&points[a].y==points[b].y;}),v.end());
        if(v.size()<3)return v;
        std::vector<int> h(2*v.size());int k=0;
        for(int x:v){while(k>=2&&cross(points[h[k-2]],points[h[k-1]],points[x])<=0)k--;h[k++]=x;}
        int t=k+1;
        for(int i=int(v.size())-2;i>=0;i--){int x=v[i];while(k>=t&&cross(points[h[k-2]],points[h[k-1]],points[x])<=0)k--;h[k++]=x;}
        h.resize(k-1);return h;
    }
    const std::vector<U64>& halfplane(int a,int b) {
        U64 key=U64(a)*P+b;auto it=edges.find(key);if(it!=edges.end())return it->second;
        std::vector<U64> bits(B,0);
        for(int p=0;p<P;p++){
            if((p&511)==0)budget.check();
            double dx=points[b].x-points[a].x,dy=points[b].y-points[a].y;
            double ex=points[p].x-points[a].x,ey=points[p].y-points[a].y;
            double eps=1e-12*(std::abs(dx*ey)+std::abs(dy*ex)+1e-100);
            if(p==a||p==b||cross(points[a],points[b],points[p])>=-eps)bits[p/64]|=U64(1)<<(p%64);
        }
        return edges.emplace(key,std::move(bits)).first->second;
    }
    void geometry(Group& g) {
        g.inside.assign(B,0);
        if(!g.active)return;
        if(g.hull.size()>=3){
            std::fill(g.inside.begin(),g.inside.end(),~U64(0));
            for(int i=0;i<int(g.hull.size());i++){
                auto& hp=halfplane(g.hull[i],g.hull[(i+1)%g.hull.size()]);
                for(int b=0;b<B;b++)g.inside[b]&=hp[b];
            }
        } else {
            Point a=points[g.hull.front()],b=points[g.hull.back()];
            for(int p=0;p<P;p++){
                double eps=1e-12*(std::abs((b.x-a.x)*(points[p].y-a.y))+std::abs((b.y-a.y)*(points[p].x-a.x))+1e-100);
                if(std::abs(cross(a,b,points[p]))<=eps&&points[p].x>=std::min(a.x,b.x)&&points[p].x<=std::max(a.x,b.x)&&points[p].y>=std::min(a.y,b.y)&&points[p].y<=std::max(a.y,b.y))g.inside[p/64]|=U64(1)<<(p%64);
            }
        }
    }
    Group build(int o,std::vector<int> v,const Group* old=nullptr) {
        budget.poll();
        Group g;g.sites=std::move(v);g.active=!g.sites.empty();
        if(!g.active)return g;
        g.owned.assign(B,0);g.owned[(n+o)/64]|=U64(1)<<((n+o)%64);
        for(int s:g.sites){g.need|=sites[s].need;g.load+=sites[s].tasks;g.gain+=profit[o][s];g.owned[s/64]|=U64(1)<<(s%64);}
        bool incremental=old&&old->active;
        if(incremental)for(int p:old->hull)if(!(g.owned[p/64]>>(p%64)&1)){incremental=false;break;}
        std::vector<int> p;
        if(incremental){p=old->hull;for(int s:g.sites)if(!(old->owned[s/64]>>(s%64)&1))p.push_back(s);}
        else {p=g.sites;p.push_back(n+o);}
        g.hull=hull(std::move(p));
        g.xmin=g.xmax=points[n+o].x;g.ymin=g.ymax=points[n+o].y;
        for(int p:g.hull){g.xmin=std::min(g.xmin,points[p].x);g.xmax=std::max(g.xmax,points[p].x);g.ymin=std::min(g.ymin,points[p].y);g.ymax=std::max(g.ymax,points[p].y);}
        if(old&&g.hull==old->hull)g.inside=old->inside;else geometry(g);
        return g;
    }
    bool allocate(const std::vector<Group>& gs,std::vector<std::vector<int>>* assignment=nullptr) {
        budget.poll();
        std::vector<int> used(K,0);
        for(auto& g:gs)for(int k=0;k<K;k++)if(g.need>>k&1)used[k]++;
        for(int k=0;k<K;k++)if(used[k]>supply[k])return false;
        if(singleton) {
            if(assignment){assignment->assign(m,{});std::vector<int> taken(w,0);for(int o=0;o<m;o++)for(int k=0;k<K;k++)if(gs[o].need>>k&1)for(int j=0;j<w;j++)if(!taken[j]&&(workers[j]>>k&1)){taken[j]=1;(*assignment)[o].push_back(j);break;}}
            return true;
        }
        std::vector<U64> need(m);for(int o=0;o<m;o++)need[o]=gs[o].need;
        std::vector<int> who(w,-1);int nodes=0;
        auto dfs=[&](auto&& self)->bool {
            budget.poll();
            if(++nodes>20000)return false;
            int oo=-1,kk=-1,bc=100000;
            for(int o=0;o<m;o++)for(int k=0;k<K;k++)if(need[o]>>k&1){
                budget.poll();int c=0;for(int j=0;j<w;j++)if(who[j]<0&&(workers[j]>>k&1))c++;
                if(!c)return false;if(c<bc){bc=c;oo=o;kk=k;}
            }
            if(oo<0)return true;
            std::vector<std::pair<int,int>> opts;
            for(int j=0;j<w;j++)if(who[j]<0&&(workers[j]>>kk&1))opts.push_back({-std::popcount(workers[j]&need[oo]),j});
            std::sort(opts.begin(),opts.end());std::vector<U64> tried;
            for(auto [val,j]:opts){if(std::find(tried.begin(),tried.end(),workers[j])!=tried.end())continue;tried.push_back(workers[j]);U64 save=need[oo];need[oo]&=~workers[j];who[j]=oo;if(self(self))return true;who[j]=-1;need[oo]=save;}
            return false;
        };
        bool ok=dfs(dfs);if(ok&&assignment){assignment->assign(m,{});for(int j=0;j<w;j++)if(who[j]>=0)(*assignment)[who[j]].push_back(j);}return ok;
    }
    double evaluate(State& s,double af=1) {
        double raw=0;int lo=1000000000,hi=0,lc=1000000000,hc=0;
        for(int o=0;o<m;o++)if(s.g[o].active){auto& g=s.g[o];raw+=g.gain-offices[o].cost;lo=std::min(lo,g.load);hi=std::max(hi,g.load);lc=std::min(lc,int(g.sites.size()));hc=std::max(hc,int(g.sites.size()));}
        s.raw=raw-alpha*std::max(0,hi-lo);
        s.score=raw-alpha*af*std::max(0,hi-lo)-countPenalty*std::max(0,hc-lc-D)-geometryPrice*s.clashes;
        return s.score;
    }
    bool validCounts(const State& s) {
        int lo=n+1,hi=0;for(auto& g:s.g)if(g.active){lo=std::min(lo,int(g.sites.size()));hi=std::max(hi,int(g.sites.size()));}return hi-lo<=D;
    }
    bool conflict(const Group& a,const Group& b) {
        if(!a.active||!b.active)return false;
        if(a.xmax<b.xmin||b.xmax<a.xmin||a.ymax<b.ymin||b.ymax<a.ymin)return false;
        for(int k=0;k<B;k++)if(a.inside[k]&b.inside[k])return true;return false;
    }
    int overlap(const Group& a,const Group& b) {
        if(!a.active||!b.active)return 0;int ans=0;
        if(a.xmax<b.xmin||b.xmax<a.xmin||a.ymax<b.ymin||b.ymax<a.ymin)return 0;
        for(int k=0;k<B;k++)ans+=std::popcount(a.inside[k]&b.inside[k]);return ans;
    }
    void recountGeometry(State& s) {
        s.clashes=0;for(int o=0;o<m;o++)for(int j=0;j<o;j++)s.clashes+=overlap(s.g[o],s.g[j]);
    }
    bool change(State& s,int a,Group ga,int b,Group gb,double af,double temp,bool force=false) {
        if(int(ga.sites.size())>offices[a].cap||(b>=0&&int(gb.sites.size())>offices[b].cap))return false;
        int newClashes=0,oldClashes=0;
        if(softGeometry||s.clashes){
            if(b>=0){newClashes+=overlap(ga,gb);oldClashes+=overlap(s.g[a],s.g[b]);}
            for(int o=0;o<m;o++)if(o!=a&&o!=b){newClashes+=overlap(ga,s.g[o]);oldClashes+=overlap(s.g[a],s.g[o]);if(b>=0){newClashes+=overlap(gb,s.g[o]);oldClashes+=overlap(s.g[b],s.g[o]);}}
            if(!softGeometry&&newClashes>oldClashes)return false;
        }else{
            if(b>=0&&conflict(ga,gb))return false;
            for(int o=0;o<m;o++)if(o!=a&&o!=b&&(conflict(ga,s.g[o])||(b>=0&&conflict(gb,s.g[o]))))return false;
        }
        Group oa=std::move(s.g[a]);s.g[a]=std::move(ga);Group ob;if(b>=0){ob=std::move(s.g[b]);s.g[b]=std::move(gb);}
        bool changedNeed=s.g[a].need!=oa.need||(b>=0&&s.g[b].need!=ob.need);
        if(changedNeed&&!allocate(s.g)) {s.g[a]=std::move(oa);if(b>=0)s.g[b]=std::move(ob);return false;}
        double oldscore=s.score,oldraw=s.raw;s.clashes+=newClashes-oldClashes;double val=evaluate(s,af);
        if(!force&&val<oldscore&&rf()>=std::exp((val-oldscore)/std::max(1e-100,temp))) {
            s.g[a]=std::move(oa);if(b>=0)s.g[b]=std::move(ob);s.score=oldscore;s.raw=oldraw;s.clashes+=oldClashes-newClashes;return false;
        }
        for(int x:oa.sites)s.owner[x]=-1;for(int x:ob.sites)s.owner[x]=-1;
        for(int x:s.g[a].sites)s.owner[x]=a;if(b>=0)for(int x:s.g[b].sites)s.owner[x]=b;
        return true;
    }
    void save(State& s) {
        if(s.clashes==0&&s.raw>bestScore+1e-8&&validCounts(s)){
            bestScore=s.raw;best=s;
            std::vector<std::vector<int>> assignment;if(!allocate(s.g,&assignment))return;
            std::ofstream f(output,std::ios::binary);f<<"[";bool first=true;
            for(int o=0;o<m;o++)if(s.g[o].active){if(!first)f<<",";first=false;f<<"{\"officeId\":"<<o<<",\"sites\":[";for(int i=0;i<int(s.g[o].sites.size());i++){if(i)f<<",";f<<s.g[o].sites[i];}f<<"],\"workers\":[";for(int i=0;i<int(assignment[o].size());i++){if(i)f<<",";f<<assignment[o][i];}f<<"]}";}f<<"]\n";
        }
    }
    void remember(const State& s) {
        if(s.clashes||s.raw<=0||!validCounts(s))return;
        for(auto& p:population){
            bool same=true;for(int o=0;o<m;o++)if(p.g[o].active!=s.g[o].active){same=false;break;}
            if(same){if(s.raw>p.raw)p=s;return;}
        }
        population.push_back(s);
        std::sort(population.begin(),population.end(),[](const State& a,const State& b){return a.raw>b.raw;});
        if(population.size()>24)population.resize(24);
    }
    State parent() {
        if(population.empty()||ri(3)==0)return best;
        return population[ri(int(population.size()))];
    }
    void singleOffices() {
        U64 have=0;for(auto v:workers)have|=v;
        for(int o=0;o<m;o++){
            budget.check();
            std::vector<int> v;for(int s=0;s<n;s++)if(!(sites[s].need&~have)&&profit[o][s]>0)v.push_back(s);
            std::sort(v.begin(),v.end(),[&](int a,int b){return profit[o][a]>profit[o][b];});if(int(v.size())>offices[o].cap)v.resize(offices[o].cap);
            State s=empty();s.g[o]=build(o,v);facilityValue[o]=std::max(unit,s.g[o].gain-offices[o].cost);for(int x:v)s.owner[x]=o;evaluate(s);save(s);
        }
    }
    bool singleOfficeProvenOptimal(){
        if(m==1)return true;
        std::vector<double> cost;for(auto& o:offices){if(o.cost<0)return false;cost.push_back(o.cost);}
        std::sort(cost.begin(),cost.end());U64 have=0;for(auto mask:workers)have|=mask;
        double upper=0;
        for(int x=0;x<n;x++)if(!(sites[x].need&~have)){double v=0;for(int o=0;o<m;o++)v=std::max(v,profit[o][x]);upper+=v;budget.poll();}
        return upper-cost[0]-cost[1]<=bestScore+1e-8;
    }
    bool move(State& s,double af,double temp,int type=-1) {
        if(type<0)type=ri(100);
        int x=ri(n),a=s.owner[x],b=ri(m);
        bool directed=ri(4)!=0;
        if(type<25){ // Addition or transfer.
            if(directed){auto& v=siteOffices[x];b=v[ri(int(v.size()))];}
            if(a==b||int(s.g[b].sites.size())>=offices[b].cap)return false;
            auto vb=s.g[b].sites;vb.push_back(x);Group gb=build(b,std::move(vb),&s.g[b]);
            if(a<0)return change(s,b,std::move(gb),-1,{},af,temp);
            auto va=s.g[a].sites;va.erase(std::find(va.begin(),va.end(),x));
            return change(s,a,build(a,std::move(va),&s.g[a]),b,std::move(gb),af,temp);
        }
        if(type<40){ // Removal.
            if(a<0)return false;auto va=s.g[a].sites;va.erase(std::find(va.begin(),va.end(),x));
            return change(s,a,build(a,std::move(va),&s.g[a]),-1,{},af,temp);
        }
        // Replacement or exchange.
        int y=ri(n);
        if(directed){
            if(a>=0&&ri(3)){
                bool sameLoad=alpha>unit*.1&&ri(4);
                for(int z=0;z<16;z++){y=candidate(a);if(s.owner[y]!=a&&(!sameLoad||sites[y].tasks==sites[x].tasks))break;}
            }
            else if(!neighbors[x].empty()){auto& v=neighbors[x];for(int z=0;z<5;z++){y=v[ri(int(v.size()))];if(s.owner[y]!=a)break;}}
        }
        b=s.owner[y];if(a==b)return false;
        if(a<0){std::swap(x,y);std::swap(a,b);}if(a<0)return false;
        auto va=s.g[a].sites;*std::find(va.begin(),va.end(),x)=y;Group ga=build(a,std::move(va),&s.g[a]);
        if(b<0)return change(s,a,std::move(ga),-1,{},af,temp);
        auto vb=s.g[b].sites;*std::find(vb.begin(),vb.end(),y)=x;
        return change(s,a,std::move(ga),b,build(b,std::move(vb),&s.g[b]),af,temp);
    }
    void greedy(State& s,double af,double noise=0,bool existingOnly=false) {
        struct Choice{double key;int o,x;};std::vector<Choice> v;
        for(int o=0;o<m;o++)if(!existingOnly||s.g[o].active)for(int x=0;x<n;x++)if(s.owner[x]<0&&profit[o][x]>0)v.push_back({profit[o][x]*(1+noise*(rf()-.5)),o,x});
        std::sort(v.begin(),v.end(),[](auto& a,auto& b){return a.key>b.key;});
        for(auto c:v){budget.poll();if(s.owner[c.x]<0&&int(s.g[c.o].sites.size())<offices[c.o].cap){auto vv=s.g[c.o].sites;vv.push_back(c.x);bool force=af==0&&s.g[c.o].active;change(s,c.o,build(c.o,std::move(vv),&s.g[c.o]),-1,{},af,unit*noise*.2,force);}}
    }
    void seedOffices(State& s) {
        int target=m==1?1:2+ri(std::max(1,std::min(m,6)-1));
        std::vector<int> order(m);std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),rng);
        for(int o:order) {
            if(target<=0)break;
            std::vector<int> v(n);std::iota(v.begin(),v.end(),0);
            std::sort(v.begin(),v.end(),[&](int a,int b){return profit[o][a]>profit[o][b];});
            for(int x:v)if(s.owner[x]<0&&profit[o][x]>0&&offices[o].cap>0){
                if(change(s,o,build(o,{x}),-1,{},0,0,true)){target--;break;}
            }
        }
    }
    void voronoi(State& s) {
        s=empty();
        std::vector<int> caps;for(auto& o:offices)if(o.cap>0)caps.push_back(o.cap);
        if(caps.empty())return;std::sort(caps.begin(),caps.end());
        int typical=caps[caps.size()/2],maxTarget=std::min(m,std::max(8,2*n/std::max(1,typical)+2));
        int target=m==1?1:2+ri(std::max(1,maxTarget-1));
        if(m>8&&ri(5)==0)target=2+ri(m-1);
        std::vector<int> order(m);std::iota(order.begin(),order.end(),0);
        if(ri(3)){
            std::vector<double> key(m);for(int o=0;o<m;o++)key[o]=std::log(std::max(1e-100,rf()))/std::max(unit*.01,facilityValue[o]);
            std::sort(order.begin(),order.end(),[&](int a,int b){return key[a]>key[b];});
        }else std::shuffle(order.begin(),order.end(),rng);
        order.resize(std::min(target,m));
        double angle=rf()*6.283185307179586,ca=std::cos(angle),sa=std::sin(angle),ratio=std::exp((rf()-.5)*2);
        auto dd=[&](int a,int b){double dx=points[a].x-points[b].x,dy=points[a].y-points[b].y;double u=dx*ca+dy*sa,v=dy*ca-dx*sa;return u*u*ratio+v*v/ratio;};
        std::vector<double> weights(m,0);
        if(ri(3))for(int z=0;z<2;z++)for(int o:order){
            double low=-1e100,high=1e100;for(int j:order)if(j!=o){high=std::min(high,dd(n+o,n+j)+weights[j]);low=std::max(low,weights[j]-dd(n+o,n+j));}
            if(high<1e99)weights[o]=low+(high-low)*(.2+.6*rf());
        }
        struct C{double value;int o,x;};std::vector<C> cand;
        for(int x=0;x<n;x++){
            int o=-1;double best=1e100;
            for(int j:order){double d=dd(x,n+j)-weights[j];if(d<best){best=d;o=j;}}
            if(o>=0&&profit[o][x]>0)cand.push_back({profit[o][x]*(.5+rf()),o,x});
        }
        std::sort(cand.begin(),cand.end(),[](auto& a,auto& b){return a.value>b.value;});
        for(auto c:cand)if(int(s.g[c.o].sites.size())<offices[c.o].cap){
            U64 old=s.g[c.o].need;s.g[c.o].need|=sites[c.x].need;
            if(allocate(s.g))s.g[c.o].sites.push_back(c.x);else s.g[c.o].need=old;
        }
        for(int o:order)s.g[o]=build(o,std::move(s.g[o].sites));
        // Discard geometrically conflicting cells if coincident coordinates or numerical ties exist.
        for(int o:order)for(int j:order)if(j<o&&conflict(s.g[o],s.g[j]))s.g[o]=Group{};
        for(int o=0;o<m;o++)for(int x:s.g[o].sites)s.owner[x]=o;
        evaluate(s);polish(s);save(s);
    }
    // Exact subset DP in both object count and task load, for difficult balance constraints.
    void countLoadShrink(State& s){
        if(D>30||alpha<=0)return;
        int groups=0,minCount=n+1,commonLoad=1000000000;std::uint64_t work=0;
        for(auto& g:s.g)if(g.active){int c=int(g.sites.size());groups++;minCount=std::min(minCount,c);commonLoad=std::min(commonLoad,g.load);work+=std::uint64_t(c)*c*(g.load+1);if(c>180||g.load>2500)return;}
        if(groups<2||work>40000000)return;
        struct Table{int o,C,W;std::vector<double> dp;std::vector<std::vector<U64>> take;};
        std::vector<Table> tables;
        for(int o=0;o<m;o++)if(s.g[o].active){
            budget.check();auto& g=s.g[o];int C=int(g.sites.size()),W=g.load+1,cells=(C+1)*W;
            Table t{o,C,W,std::vector<double>(cells,-1e100),{}};t.dp[0]=0;
            t.take.assign(C,std::vector<U64>((cells+63)/64));int reach=0;
            for(int i=0;i<C;i++){
                budget.check();int x=g.sites[i],load=sites[x].tasks;reach+=load;
                for(int c=i+1;c>0;c--)for(int j=reach;j>=load;j--){
                    int to=c*W+j,from=(c-1)*W+j-load;
                    if(t.dp[from]>-1e90&&t.dp[from]+profit[o][x]>t.dp[to]+1e-9){t.dp[to]=t.dp[from]+profit[o][x];t.take[i][to/64]|=U64(1)<<(to%64);}
                }
            }
            tables.push_back(std::move(t));
        }
        int stride=commonLoad+1;std::vector<double> value((minCount+1)*stride,0);
        std::vector<int> valid(value.size(),0);
        for(auto& t:tables){
            std::vector<int> deque(t.C+1);
            for(int load=0;load<=commonLoad;load++){
                if((load&31)==0)budget.check();int head=0,tail=0;
                for(int c=t.C;c>0;c--){
                    double v=t.dp[c*t.W+load];
                    while(head<tail&&t.dp[deque[tail-1]*t.W+load]<=v)tail--;
                    deque[tail++]=c;while(head<tail&&deque[head]>c+D)head++;
                    if(c<=minCount&&head<tail){double best=t.dp[deque[head]*t.W+load];if(best>-1e90){int idx=c*stride+load;value[idx]+=best;valid[idx]++;}}
                }
            }
        }
        double cost=0;for(auto& t:tables)cost+=offices[t.o].cost;
        double bestValue=validCounts(s)?s.raw:-1e100;int bestCount=-1,bestLoad=-1;
        for(int c=1;c<=minCount;c++)for(int load=0;load<=commonLoad;load++){
            int idx=c*stride+load;if(valid[idx]==groups&&value[idx]-cost>bestValue+1e-7){bestValue=value[idx]-cost;bestCount=c;bestLoad=load;}
        }
        if(bestCount<0)return;
        for(auto& t:tables){
            int count=bestCount;
            for(int c=bestCount+1;c<=std::min(t.C,bestCount+D);c++)if(t.dp[c*t.W+bestLoad]>t.dp[count*t.W+bestLoad])count=c;
            int load=bestLoad;auto& g=s.g[t.o];std::vector<int> v;
            for(int i=t.C-1;i>=0;i--){int cell=count*t.W+load;if(t.take[i][cell/64]>>(cell%64)&1){int x=g.sites[i];v.push_back(x);count--;load-=sites[x].tasks;}}
            s.g[t.o]=build(t.o,std::move(v));
        }
        s.owner.assign(n,-1);for(int o=0;o<m;o++)for(int x:s.g[o].sites)s.owner[x]=o;
        recountGeometry(s);evaluate(s);save(s);
    }
    void polish(State& s){
        evaluate(s);
        State exact;
        bool tryCount=D<=30&&alpha>0;
        if(tryCount){exact=s;countLoadShrink(exact);}
        balanceShrink(s);
        if(tryCount&&exact.score>s.score+1e-7)s=std::move(exact);
    }
    // Exact knapsack over subsets of each zone. Shrinking cannot create a hull conflict.
    void balanceShrink(State& s) {
        struct Table {int o,L;std::vector<double> dp;std::vector<int> count;std::vector<std::vector<U64>> take;};
        std::vector<Table> tables;int maxL=0;
        for(int o=0;o<m;o++)if(s.g[o].active){
            auto& g=s.g[o];int L=g.load;maxL=std::max(maxL,L);if(L>5000)return;
            Table t;t.o=o;t.L=L;t.dp.assign(L+1,-1e100);t.count.assign(L+1,0);t.dp[0]=0;
            t.take.assign(g.sites.size(),std::vector<U64>((L+64)/64));
            int reach=0;
            for(int i=0;i<int(g.sites.size());i++){
                budget.check();
                int x=g.sites[i],load=sites[x].tasks;double val=profit[o][x];reach+=load;
                for(int j=reach;j>=load;j--)if(t.dp[j-load]>-1e90&&t.dp[j-load]+val>t.dp[j]+1e-9){
                    t.dp[j]=t.dp[j-load]+val;t.count[j]=t.count[j-load]+1;t.take[i][j/64]|=U64(1)<<(j%64);
                }
            }
            tables.push_back(std::move(t));
        }
        if(tables.size()<2||maxL>5000)return;
        std::vector<int> bestLoads;double bv=validCounts(s)?s.raw:-1e100;
        if(maxL>300||D>=n){
            struct Segment {int l,r,arg;double value;};
            std::vector<std::vector<Segment>> stacks(tables.size());
            IntervalTree tree(maxL+1,int(tables.size()),alpha);
            double cost=0;for(auto& t:tables)cost+=offices[t.o].cost;
            for(int hi=0;hi<=maxL;hi++){
                budget.check();
                for(int k=0;k<int(tables.size());k++){
                    auto& t=tables[k];auto& stack=stacks[k];
                    if(!stack.empty()&&stack.back().arg<0)stack.back().r=hi;
                    else stack.push_back({hi,hi,-1,0});
                    if(hi<=t.L&&t.dp[hi]>-1e90&&t.count[hi]>0){
                        double val=t.dp[hi];int left=hi;
                        while(!stack.empty()&&(stack.back().arg<0||stack.back().value<=val)){
                            auto v=stack.back();stack.pop_back();
                            tree.add(v.l,v.r,v.arg<0?-1:0,val-(v.arg<0?0:v.value));left=v.l;
                        }
                        stack.push_back({left,hi,hi,val});
                    }
                }
                std::vector<int> excluded;
                for(int attempt=0;attempt<8&&tree.tree[1].missing==0;attempt++){
                    double val=tree.tree[1].value-alpha*hi-cost;
                    if(val<=bv+1e-7)break;
                    int lo=tree.tree[1].arg,cmin=n+1,cmax=0;std::vector<int> arg;
                    for(int k=0;k<int(tables.size());k++){
                        auto& stack=stacks[k];int l=0,r=int(stack.size());
                        while(l+1<r){int mid=(l+r)/2;if(stack[mid].l<=lo)l=mid;else r=mid;}
                        int load=stack[l].arg;arg.push_back(load);
                        cmin=std::min(cmin,tables[k].count[load]);cmax=std::max(cmax,tables[k].count[load]);
                    }
                    if(cmax-cmin<=D){bv=val;bestLoads=std::move(arg);break;}
                    tree.add(lo,lo,int(tables.size())+1,0);excluded.push_back(lo);
                }
                for(int lo:excluded)tree.add(lo,lo,-int(tables.size())-1,0);
            }
        }else{
        // Exhaustive count-aware fallback on small load tables.
        for(int lo=0;lo<=maxL;lo++){
            budget.check();
            std::vector<int> arg(tables.size(),-1);
            for(int hi=lo;hi<=maxL;hi++){
                if((hi&63)==0)budget.check();
                double val=-alpha*(hi-lo);int cmin=n+1,cmax=0;bool ok=true;
                for(int k=0;k<int(tables.size());k++){
                    auto& t=tables[k];
                    if(hi<=t.L&&t.dp[hi]>-1e90&&t.count[hi]>0&&(arg[k]<0||t.dp[hi]>t.dp[arg[k]]))arg[k]=hi;
                    if(arg[k]<0){ok=false;continue;}
                    val+=t.dp[arg[k]]-offices[t.o].cost;
                    cmin=std::min(cmin,t.count[arg[k]]);cmax=std::max(cmax,t.count[arg[k]]);
                }
                if(ok&&cmax-cmin<=D&&val>bv+1e-7){bv=val;bestLoads=arg;}
                if(alpha*(hi-lo)>unit*n&&alpha>0)break;
            }
        }
        }
        if(bestLoads.empty())return;
        for(int k=0;k<int(tables.size());k++){
            auto& t=tables[k];auto& g=s.g[t.o];int j=bestLoads[k];std::vector<int> v;
            for(int i=int(g.sites.size())-1;i>=0;i--)if(t.take[i][j/64]>>(j%64)&1){int x=g.sites[i];v.push_back(x);j-=sites[x].tasks;}
            s.g[t.o]=build(t.o,std::move(v));
        }
        s.owner.assign(n,-1);for(int o=0;o<m;o++)for(int x:s.g[o].sites)s.owner[x]=o;recountGeometry(s);evaluate(s);save(s);
    }
    bool balancedMove(State& s,double temp) {
        std::vector<int> order;for(int o=0;o<m;o++)if(s.g[o].active)order.push_back(o);
        if(order.size()<2)return false;std::shuffle(order.begin(),order.end(),rng);
        int delta=1+ri(4);if(ri(4)==0)delta=-delta;
        State backup=s;
        for(int o:order){
            budget.check();
            struct Opt{double val;int x,y;};std::vector<Opt> opts;
            auto& g=s.g[o];
            for(int z=0;z<100;z++){
                int x=ri(3)?candidate(o):ri(n);if(s.owner[x]>=0)continue;
                if(sites[x].tasks==delta&&int(g.sites.size())<offices[o].cap)opts.push_back({profit[o][x],x,-1});
                if(!g.sites.empty()){
                    int y=g.sites[ri(int(g.sites.size()))];
                    if(sites[x].tasks-sites[y].tasks==delta)opts.push_back({profit[o][x]-profit[o][y],x,y});
                }
            }
            if(delta<0)for(int y:g.sites)if(sites[y].tasks==-delta&&g.sites.size()>1)opts.push_back({-profit[o][y],-1,y});
            std::sort(opts.begin(),opts.end(),[](auto& a,auto& b){return a.val>b.val;});
            bool done=false;
            for(auto c:opts){
                auto v=s.g[o].sites;
                if(c.y>=0)v.erase(std::find(v.begin(),v.end(),c.y));if(c.x>=0)v.push_back(c.x);
                if(change(s,o,build(o,std::move(v),&s.g[o]),-1,{},1,0,true)){done=true;break;}
            }
            if(!done){s=std::move(backup);return false;}
        }
        if(s.score<backup.score&&rf()>=std::exp((s.score-backup.score)/std::max(1e-100,temp))){s=std::move(backup);return false;}
        return true;
    }
    bool relocate(State& s,double temp) {
        int a=ri(m),b=ri(m);if(a==b||!s.g[a].active||s.g[b].active)return false;
        return change(s,a,{},b,build(b,s.g[a].sites),1,temp);
    }
    void repairCounts(State& s) {
        for(int iteration=0;iteration<n;iteration++){
            budget.check();
            int lo=n+1,hi=0,o=-1;
            for(int j=0;j<m;j++)if(s.g[j].active){int c=int(s.g[j].sites.size());lo=std::min(lo,c);if(c>hi){hi=c;o=j;}}
            if(o<0||hi-lo<=D)break;
            int chosen=-1;double val=-1e100;
            auto& g=s.g[o];
            for(int x:g.sites){
                int minload=1000000000,maxload=0;
                for(int j=0;j<m;j++)if(s.g[j].active){int load=s.g[j].load-(j==o?sites[x].tasks:0);minload=std::min(minload,load);maxload=std::max(maxload,load);}
                double v=-profit[o][x]-alpha*(maxload-minload);
                if(v>val){val=v;chosen=x;}
            }
            auto v=g.sites;v.erase(std::find(v.begin(),v.end(),chosen));s.owner[chosen]=-1;s.g[o]=build(o,std::move(v));
        }
        recountGeometry(s);evaluate(s);save(s);
    }
    void ruinGrow(State& s) {
        s=parent();
        int mode=ri(12);
        if(mode>=9){pairRebuild(s);return;}
        if(mode==0){int o=ri(m);s.g[o]=Group{};}
        else if(mode>=7){
            int o=ri(m);
            if(!s.g[o].active){
                // Make room around a new office before rebuilding the zones.
                std::vector<int> near(n);std::iota(near.begin(),near.end(),0);
                std::sort(near.begin(),near.end(),[&](int a,int b){return profit[o][a]>profit[o][b];});
                int x=near.front();
                for(int j=0;j<m;j++)if(s.g[j].active){
                    bool drop=(s.g[j].inside[(n+o)/64]>>((n+o)%64)&1)||(s.g[j].inside[x/64]>>(x%64)&1);
                    if(drop){std::vector<int> v;for(int p:s.g[j].sites){double ax=points[p].x-points[n+o].x,ay=points[p].y-points[n+o].y,bx=points[p].x-points[n+j].x,by=points[p].y-points[n+j].y;if(ax*ax+ay*ay>bx*bx+by*by)v.push_back(p);}s.g[j]=build(j,std::move(v));}
                }
                s.owner.assign(n,-1);for(int j=0;j<m;j++)for(int p:s.g[j].sites)s.owner[p]=j;
                evaluate(s,0);
                for(int p:near)if(s.owner[p]<0&&profit[o][p]>0&&change(s,o,build(o,{p}),-1,{},0,0,true))break;
            }
        }
        else if(mode>=5&&K){
            int skill=ri(K),o=ri(m);
            for(int i=0;i<m;i++)if(s.g[i].need>>skill&1){o=i;break;}
            std::vector<int> v;for(int x:s.g[o].sites)if(!(sites[x].need>>skill&1))v.push_back(x);
            s.g[o]=build(o,std::move(v));
        }
        else if(mode==2){
            int center=ri(n);std::vector<int> near(n);std::iota(near.begin(),near.end(),0);
            auto dist=[&](int x){double dx=points[x].x-points[center].x,dy=points[x].y-points[center].y;return dx*dx+dy*dy;};
            std::sort(near.begin(),near.end(),[&](int a,int b){return dist(a)<dist(b);});
            std::vector<char> erase(n,0);for(int i=0;i<std::max(1,n/5);i++)erase[near[i]]=1;
            for(int o=0;o<m;o++)if(s.g[o].active){std::vector<int> v;for(int x:s.g[o].sites)if(!erase[x])v.push_back(x);s.g[o]=build(o,std::move(v));}
        }
        else {
            for(int o=0;o<m;o++)if(s.g[o].active){
                std::vector<int> v;for(int x:s.g[o].sites)if(rf()>(mode==1?.45:.15))v.push_back(x);
                s.g[o]=build(o,std::move(v));
            }
        }
        s.owner.assign(n,-1);for(int o=0;o<m;o++)for(int x:s.g[o].sites)s.owner[x]=o;
        evaluate(s,0);greedy(s,0,rf()*1.2,true);polish(s);
    }
    void pairRebuild(State& s){
        std::vector<int> active;for(int o=0;o<m;o++)if(s.g[o].active)active.push_back(o);
        if(active.empty()||m<2)return;
        int a=active[ri(int(active.size()))],b=ri(m);if(a==b)b=(b+1)%m;
        double angle=rf()*6.283185307179586,nx=std::cos(angle),ny=std::sin(angle);
        auto projection=[&](int p){return points[p].x*nx+points[p].y*ny;};
        double pa=projection(n+a),pb=projection(n+b);if(pa==pb)return;
        bool aLeft=pa<pb;double cut=pa+(pb-pa)*(.1+.8*rf());
        s.g[a]=Group{};s.g[b]=Group{};
        for(int x=0;x<n;x++)if(s.owner[x]==a||s.owner[x]==b)s.owner[x]=-1;
        evaluate(s,0);
        struct Candidate{double value;int o,x;};std::vector<Candidate> candidates;
        for(int x=0;x<n;x++)if(s.owner[x]<0){int o=(projection(x)<cut)==aLeft?a:b;if(profit[o][x]>0)candidates.push_back({profit[o][x]*(.75+.5*rf()),o,x});}
        std::sort(candidates.begin(),candidates.end(),[](auto& x,auto& y){return x.value>y.value;});
        for(auto c:candidates){budget.poll();if(int(s.g[c.o].sites.size())>=offices[c.o].cap)continue;auto v=s.g[c.o].sites;v.push_back(c.x);change(s,c.o,build(c.o,std::move(v),&s.g[c.o]),-1,{},0,0,true);}
        evaluate(s);polish(s);if(!validCounts(s))repairCounts(s);save(s);
    }
    void run() {
        try {
        singleOffices();
        if(singleOfficeProvenOptimal()){std::cerr<<"proven_optimal score="<<bestScore<<" elapsed="<<elapsed()<<"\n";return;}
        prepareCandidates();
        State current=best;int epoch=0;double progress=elapsed();
        while(elapsed()<seconds){
            softGeometry=false;geometryPrice=unit*40;
            double af=1;double duration=.45;
            if(epoch%3==0){voronoi(current);af=1;}
            else if(epoch%3==1){ruinGrow(current);}
            else current=parent();
            double began=elapsed();
            while(elapsed()<seconds&&elapsed()-began<duration){
                double f=(elapsed()-began)/duration;
                softGeometry=epoch%3==0&&f<.7;
                geometryPrice=unit*(.5+40*f*f);
                af=epoch%3==0?std::min(1.0,f*2):1;
                double temp=unit*std::pow(.002,f)*(epoch%3==0?.7:.12);
                evaluate(current,af);
                for(int j=0;j<128;j++){budget.poll();move(current,af,temp);save(current);iterations++;}
                balancedMove(current,temp);save(current);
                relocate(current,temp);save(current);
                remember(current);
            }
            evaluate(current);save(current);polish(current);if(!validCounts(current))repairCounts(current);remember(current);epoch++;
            if(edges.size()*std::size_t(B)*8>300000000)edges.clear();
            if(elapsed()-progress>=5){std::cerr<<"progress "<<elapsed()<<" score="<<bestScore<<"\n";progress=elapsed();}
        }
        }catch(const SearchTimeout&){}
        std::cerr<<"score="<<bestScore<<" elapsed="<<elapsed()<<" iterations="<<iterations<<" offices=";int c=0;for(auto& g:best.g)c+=g.active;std::cerr<<c<<"\n";
    }
};
} // namespace

// A safe general fallback when a data set has more than 64 distinct skill IDs.
static void large_skill_solution(const Json& root,const char* output) {
    const auto& ss=root.find("sites")->array;const auto& os=root.find("offices")->array;
    const auto& ws=root.find("workers")->array;const auto& ds=root.find("distances")->array;
    std::unordered_set<int> available;for(const auto& row:ws)for(const auto& v:row.array)available.insert(int(v.number));
    double bestValue=0;int bestOffice=-1;std::vector<int> chosen;
    for(int o=0;o<int(os.size());o++){
        if(activeBudget)activeBudget->check();
        std::vector<std::pair<double,int>> cand;
        for(int s=0;s<int(ss.size());s++){
            if(activeBudget)activeBudget->poll();
            bool ok=true;for(const auto& t:ss[s].find("tasks")->array)if(!available.count(int(t.number))){ok=false;break;}
            double val=ss[s].find("reward")->number-ds[o].array[s].number;if(ok&&val>0)cand.push_back({val,s});
        }
        std::sort(cand.rbegin(),cand.rend());int cap=std::max(0,int(os[o].find("maxSize")->number));if(int(cand.size())>cap)cand.resize(cap);
        double value=-os[o].find("cost")->number;for(auto [v,s]:cand)value+=v;
        if(!cand.empty()&&value>bestValue){bestValue=value;bestOffice=o;chosen.clear();for(auto [v,s]:cand)chosen.push_back(s);}
    }
    std::ofstream f(output,std::ios::binary);f<<"[";
    if(bestOffice>=0){f<<"{\"officeId\":"<<bestOffice<<",\"sites\":[";for(int i=0;i<int(chosen.size());i++){if(i)f<<",";f<<chosen[i];}f<<"],\"workers\":[";for(int j=0;j<int(ws.size());j++){if(j)f<<",";f<<j;}f<<"]}";}
    f<<"]\n";
}

int main(int argc,char** argv) {
    try {
        const char* input=argc>1?argv[1]:"instance.json";
        const char* output=argc>2?argv[2]:"solution.json";
        double seconds=argc>3?std::atof(argv[3]):270.0;
        Budget timer(seconds);activeBudget=&timer;seconds=timer.limit;
        {std::ofstream f(output,std::ios::binary);f<<"[]\n";}
        U64 seed=argc>4?std::strtoull(argv[4],nullptr,10):712367821;
        std::string text=read_file(input);if(text.size()>=3&&(unsigned char)text[0]==0xef)text.erase(0,3);
        std::size_t pos=0;Json root=Json::parse(text,pos);
        if(root.find("sites")->array.empty()||root.find("offices")->array.empty()){std::ofstream f(output);f<<"[]\n";return 0;}
        std::unordered_set<int> allskills;
        for(const auto& s:root.find("sites")->array)for(const auto& t:s.find("tasks")->array)allskills.insert(int(t.number));
        for(const auto& w:root.find("workers")->array)for(const auto& t:w.array)allskills.insert(int(t.number));
        if(allskills.size()>64){large_skill_solution(root,output);return 0;}
        Solver solver(root,seconds,seed,output,timer);
        root=Json{};std::string{}.swap(text); // Release the much larger JSON syntax tree before search.
        timer.check();solver.run();return 0;
    }catch(const SearchTimeout&){return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}


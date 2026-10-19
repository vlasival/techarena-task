// TechArena: self-contained C++20 solver, no external dependencies.
// Reads instance.json and repeatedly writes the best feasible solution.json.

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
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
    std::vector<U64> inside;
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

struct Solver {
    int n,m,w,K,P,B,D;
    double alpha,unit=1,countPenalty;
    std::vector<Point> points;
    std::vector<Site> sites;
    std::vector<Office> offices;
    std::vector<std::vector<double>> profit;
    std::vector<U64> workers;
    std::vector<int> supply;
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

    double elapsed() const { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); }
    int ri(int a) { return int(rng()%a); }
    double rf() { return (rng()>>11)*0x1.0p-53; }
    static double cross(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
    Solver(const Json& root, double sec, U64 seed, std::string out): rng(seed),seconds(sec),output(std::move(out)) {
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
        for(int o=0;o<m;o++)for(int s=0;s<n;s++){profit[o][s]=sites[s].reward-dj[o].array[s].number;if(profit[o][s]>0){sum+=profit[o][s];cnt++;}}
        unit=cnt?sum/cnt:1;countPenalty=unit*5+alpha;
        best=empty();
    }
    State empty() { State s;s.g.resize(m);s.owner.assign(n,-1);return s; }
    std::vector<int> hull(std::vector<int> v) {
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
        Group g;g.sites=std::move(v);g.active=!g.sites.empty();
        if(!g.active)return g;
        for(int s:g.sites){g.need|=sites[s].need;g.load+=sites[s].tasks;g.gain+=profit[o][s];}
        std::vector<int> p=g.sites;p.push_back(n+o);g.hull=hull(std::move(p));
        if(old&&g.hull==old->hull)g.inside=old->inside;else geometry(g);
        return g;
    }
    bool allocate(const std::vector<Group>& gs,std::vector<std::vector<int>>* assignment=nullptr) {
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
            if(++nodes>20000)return false;
            int oo=-1,kk=-1,bc=100000;
            for(int o=0;o<m;o++)for(int k=0;k<K;k++)if(need[o]>>k&1){
                int c=0;for(int j=0;j<w;j++)if(who[j]<0&&(workers[j]>>k&1))c++;
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
        for(int k=0;k<B;k++)if(a.inside[k]&b.inside[k])return true;return false;
    }
    int overlap(const Group& a,const Group& b) {
        if(!a.active||!b.active)return 0;int ans=0;
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
        if(!allocate(s.g)) {s.g[a]=std::move(oa);if(b>=0)s.g[b]=std::move(ob);return false;}
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
            std::vector<int> v;for(int s=0;s<n;s++)if(!(sites[s].need&~have)&&profit[o][s]>0)v.push_back(s);
            std::sort(v.begin(),v.end(),[&](int a,int b){return profit[o][a]>profit[o][b];});if(int(v.size())>offices[o].cap)v.resize(offices[o].cap);
            State s=empty();s.g[o]=build(o,v);for(int x:v)s.owner[x]=o;evaluate(s);save(s);
        }
    }
    bool move(State& s,double af,double temp,int type=-1) {
        if(type<0)type=ri(100);
        int x=ri(n),a=s.owner[x],b=ri(m);
        if(type<25){ // Addition or transfer.
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
        int y=ri(n);b=s.owner[y];if(a==b)return false;
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
        for(auto c:v)if(s.owner[c.x]<0&&int(s.g[c.o].sites.size())<offices[c.o].cap){auto vv=s.g[c.o].sites;vv.push_back(c.x);change(s,c.o,build(c.o,std::move(vv),&s.g[c.o]),-1,{},af,unit*noise*.2);}
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
        int target=m==1?1:2+ri(std::max(1,std::min(m,8)-1));
        std::vector<int> order(m);std::iota(order.begin(),order.end(),0);std::shuffle(order.begin(),order.end(),rng);order.resize(std::min(target,m));
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
        evaluate(s);balanceShrink(s);save(s);
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
                int x=g.sites[i],load=sites[x].tasks;double val=profit[o][x];reach+=load;
                for(int j=reach;j>=load;j--)if(t.dp[j-load]>-1e90&&t.dp[j-load]+val>t.dp[j]+1e-9){
                    t.dp[j]=t.dp[j-load]+val;t.count[j]=t.count[j-load]+1;t.take[i][j/64]|=U64(1)<<(j%64);
                }
            }
            tables.push_back(std::move(t));
        }
        if(tables.size()<2||maxL>5000)return;
        std::vector<int> bestLoads;double bv=s.raw;
        // All intervals suffice: independently choose the best subset in the load interval.
        for(int lo=0;lo<=maxL;lo++){
            if((lo&31)==0&&elapsed()>seconds)return;
            std::vector<int> arg(tables.size(),-1);
            for(int hi=lo;hi<=maxL;hi++){
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
            struct Opt{double val;int x,y;};std::vector<Opt> opts;
            auto& g=s.g[o];
            for(int z=0;z<100;z++){
                int x=ri(n);if(s.owner[x]>=0)continue;
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
        int mode=ri(9);
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
        evaluate(s,0);greedy(s,0,rf()*1.2,true);balanceShrink(s);
    }
    void run() {
        {std::ofstream f(output);f<<"[]\n";}
        singleOffices();
        State current=best;int epoch=0;
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
                for(int j=0;j<128;j++){move(current,af,temp);save(current);iterations++;}
                balancedMove(current,temp);save(current);
                relocate(current,temp);save(current);
                remember(current);
            }
            evaluate(current);save(current);balanceShrink(current);if(!validCounts(current))repairCounts(current);remember(current);epoch++;
            if(edges.size()*std::size_t(B)*8>300000000)edges.clear();
        }
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
        std::vector<std::pair<double,int>> cand;
        for(int s=0;s<int(ss.size());s++){
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
        double seconds=argc>3?std::atof(argv[3]):285.0;
        U64 seed=argc>4?std::strtoull(argv[4],nullptr,10):712367821;
        std::string text=read_file(input);if(text.size()>=3&&(unsigned char)text[0]==0xef)text.erase(0,3);
        std::size_t pos=0;Json root=Json::parse(text,pos);
        if(root.find("sites")->array.empty()||root.find("offices")->array.empty()){std::ofstream f(output);f<<"[]\n";return 0;}
        std::unordered_set<int> allskills;
        for(const auto& s:root.find("sites")->array)for(const auto& t:s.find("tasks")->array)allskills.insert(int(t.number));
        for(const auto& w:root.find("workers")->array)for(const auto& t:w.array)allskills.insert(int(t.number));
        if(allskills.size()>64){large_skill_solution(root,output);return 0;}
        Solver solver(root,seconds,seed,output);solver.run();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}


#define main submission_main
#include "../main.cpp"
#undef main
int main(){
 std::ostringstream q;q<<"{\"sites\":[";std::vector<int> load(50),gain(50);
 for(int x=0;x<50;x++){if(x)q<<',';load[x]=x<10||x>=40?2:1;gain[x]=x<10?10:x<20?8:x<40?7:5;q<<"{\"x\":"<<(x<20?0:100)<<",\"y\":"<<x<<",\"reward\":100,\"tasks\":[0";if(load[x]==2)q<<",0";q<<"]}";}
 q<<"],\"offices\":[{\"x\":0,\"y\":0,\"cost\":1,\"maxSize\":20},{\"x\":100,\"y\":20,\"cost\":1,\"maxSize\":30}],\"workers\":[[0],[0]],\"distances\":[";
 for(int o=0;o<2;o++){if(o)q<<',';q<<'[';for(int x=0;x<50;x++){if(x)q<<',';q<<(int(x>=20)==o?100-gain[x]:200);}q<<']';}
 q<<"],\"maxDifference\":0,\"balanceCoefficient\":100}";std::string txt=q.str();std::size_t pos=0;auto root=Json::parse(txt,pos);Budget timer(10);Solver sol(root,10,1,"work/count_load_solution.json",timer);auto s=sol.empty();
 for(int o=0;o<2;o++){std::vector<int> v;for(int x=0;x<50;x++)if(int(x>=20)==o){v.push_back(x);s.owner[x]=o;}s.g[o]=sol.build(o,v);}
 sol.evaluate(s);sol.countLoadShrink(s);
 if(std::abs(s.raw-298)>1e-8||!sol.validCounts(s)||s.g[0].load!=30||s.g[1].load!=30){std::cerr<<"FAIL "<<s.raw<<'\n';return 1;}
 std::ofstream f("work/count_load_instance.json");f<<txt;
 std::cout<<"PASS: joint count/load DP finds 298 with 20 objects and 30 tasks per office.\n";
}

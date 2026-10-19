#define main submission_main
#include "../main.cpp"
#undef main
#include <cassert>

int main(){
 std::mt19937 gen(923423);
 for(int trial=0;trial<80;trial++){
  int m=2+gen()%4,k=25+gen()%15,n=m*k;
  std::ostringstream q;q<<"{\"sites\":[";
  std::vector<int> loads(n);std::vector<std::vector<double>> values(m,std::vector<double>(n));
  for(int x=0;x<n;x++){
   if(x)q<<',';loads[x]=1+gen()%20;
   q<<"{\"x\":"<<x/k*100+(x%k)*.01<<",\"y\":"<<x%k<<",\"reward\":1000,\"tasks\":[";
   for(int j=0;j<loads[x];j++){if(j)q<<',';q<<0;}q<<"]}";
  }
  q<<"],\"offices\":[";for(int o=0;o<m;o++){if(o)q<<',';q<<"{\"x\":"<<o*100<<",\"y\":0,\"cost\":10,\"maxSize\":"<<n<<'}';}
  q<<"],\"workers\":[";for(int o=0;o<m;o++){if(o)q<<',';q<<"[0]";}
  q<<"],\"distances\":[";for(int o=0;o<m;o++){if(o)q<<',';q<<'[';for(int x=0;x<n;x++){if(x)q<<',';values[o][x]=1+gen()%999;q<<1000-values[o][x];}q<<']';}
  double alpha=1+gen()%100;q<<"],\"maxDifference\":"<<n<<",\"balanceCoefficient\":"<<alpha<<'}';
  std::string text=q.str();std::size_t p=0;Json root=Json::parse(text,p);Budget timer(20);Solver sol(root,20,1,"work/interval_test_solution.json",timer);auto state=sol.empty();
  std::vector<std::vector<double>> dp(m);int T=0;
  for(int o=0;o<m;o++){
   std::vector<int> vv;int sum=0;for(int x=o*k;x<(o+1)*k;x++){vv.push_back(x);state.owner[x]=o;sum+=loads[x];}
   state.g[o]=sol.build(o,vv);dp[o].assign(sum+1,-1e100);dp[o][0]=0;int reach=0;
   for(int x:vv){reach+=loads[x];for(int t=reach;t>=loads[x];t--)dp[o][t]=std::max(dp[o][t],dp[o][t-loads[x]]+values[o][x]);}
   dp[o][0]=-1e100;T=std::max(T,sum);
  }
  sol.evaluate(state);double exact=state.raw;
  for(int lo=0;lo<=T;lo++){
   std::vector<double> maxv(m,-1e100);
   for(int hi=lo;hi<=T;hi++){
    double score=-alpha*(hi-lo)-10*m;
    for(int o=0;o<m;o++){if(hi<int(dp[o].size()))maxv[o]=std::max(maxv[o],dp[o][hi]);score+=maxv[o];}
    exact=std::max(exact,score);
   }
  }
  sol.balanceShrink(state);
  if(std::abs(state.raw-exact)>1e-6){std::cerr<<"FAIL trial="<<trial<<" got="<<state.raw<<" expected="<<exact<<'\n';return 1;}
 }
 std::cout<<"PASS: 80 independent comparisons against exhaustive interval optimization.\n";
}

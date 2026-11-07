// Original472F30: retail240SX_CV, COM(.25,0,.28), identity axes.
{ContactVector d{{0x1.0000000000000p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==7 && point==ContactVector{{0x1.0eee160000000p+1F,0x1.99966a0000000p-12F,-0x1.89455c0000000p-3F}},"native support index and world point");}
{ContactVector d{{0x1.0000000000000p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==7 && point==ContactVector{{0x1.0eee160000000p+1F,0x1.99966a0000000p-12F,-0x1.89455c0000000p-3F}},"native support index and world point");}
{ContactVector d{{-0x1.0000000000000p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==4 && point==ContactVector{{-0x1.26d9260000000p+1F,-0x1.0207720000000p-11F,-0x1.8d82d40000000p-4F}},"native support index and world point");}
{ContactVector d{{-0x1.0000000000000p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==4 && point==ContactVector{{-0x1.26d9260000000p+1F,-0x1.0207720000000p-11F,-0x1.8d82d40000000p-4F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x1.0000000000000p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==11 && point==ContactVector{{0x1.4bdfa60000000p+0F,0x1.bf66400000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x1.0000000000000p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==11 && point==ContactVector{{0x1.4bdfa60000000p+0F,0x1.bf66400000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,-0x1.0000000000000p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==3 && point==ContactVector{{0x1.4bdfa60000000p+0F,-0x1.c2f0780000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,-0x1.0000000000000p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==3 && point==ContactVector{{0x1.4bdfa60000000p+0F,-0x1.c2f0780000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,0x1.0000000000000p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==17 && point==ContactVector{{0x1.3721b80000000p-4F,-0x1.31d6840000000p-12F,0x1.83a6700000000p-1F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,0x1.0000000000000p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==17 && point==ContactVector{{0x1.3721b80000000p-4F,-0x1.31d6840000000p-12F,0x1.83a6700000000p-1F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,-0x1.0000000000000p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==2 && point==ContactVector{{0x1.3721b00000000p-4F,-0x1.b10e720000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,-0x1.0000000000000p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==11 && point==ContactVector{{0x1.4bdfa60000000p+0F,0x1.bf66400000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}
{ContactVector d{{0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.6666666666666p-1F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==18 && point==ContactVector{{0x1.0eede80000000p+1F,0x1.9994360000000p-12F,0x1.1010e40000000p-3F}},"native support index and world point");}
{ContactVector d{{0x1.0000000000000p+0F,0x1.999999999999ap-3F,0x1.6666666666666p-1F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==18 && point==ContactVector{{0x1.0eede80000000p+1F,0x1.9994360000000p-12F,0x1.1010e40000000p-3F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,0,index,point),"native support executes");
check(index==0 && point==ContactVector{{-0x1.219afa0000000p+1F,-0x1.131f8c0000000p-1F,-0x1.b6da400000000p-4F}},"native support index and world point");}
{ContactVector d{{0x0.0p+0F,0x0.0p+0F,0x0.0p+0F}};
unsigned index=999;ContactVector point{};
check(race_hull_support(first,spec,box,d,11,index,point),"native support executes");
check(index==11 && point==ContactVector{{0x1.4bdfa60000000p+0F,0x1.bf66400000000p-1F,-0x1.8945620000000p-3F}},"native support index and world point");}

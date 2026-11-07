// 46EA50: ordered point/edge, edge/edge, then face/point dispatch.
// All feature coordinates and arithmetic are binary32 (retail x87 PC=24).
using F = ContactVector;
F fv(V a) { return {float(a.x),float(a.y),float(a.z)}; }
V dv(F a) { return {a[0],a[1],a[2]}; }
F fa(F a,F b) { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
F fs(F a,F b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
F fm(F a,float k) { return {a[0]*k,a[1]*k,a[2]*k}; }
float fd(F a,F b) { return (a[2]*b[2]+a[1]*b[1])+a[0]*b[0]; }
float dxzy(F a,F b) { return (a[0]*b[0]+a[2]*b[2])+a[1]*b[1]; }
float dxyz(F a,F b) { return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2]; }
float dyzx(F a,F b) { return (a[1]*b[1]+a[2]*b[2])+a[0]*b[0]; }
F fc(F a,F b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
F fu(F a,bool face=false) {
    const float length=float(vehicle_sqrt(face?dxzy(a,a):dxyz(a,a)));
    return length!=0?fm(a,1.F/length):F{1,0,0};
}
F perpendicular(F r,F edge) { return fc(edge,fc(r,edge)); }
float clamp_feature(float x) { return std::max(0.F,std::min(1.F,x)); }
struct FeatureChoice {
    F a{},b{},direction{};
    std::array<unsigned,3> ia{},ib{};
    unsigned na=0,nb=0;
    float squared=std::numeric_limits<float>::max();
};
bool closest(Feature& a,Feature& b,V& wa,V& wb,V& direction,double& squared) {
    FeatureChoice best;
    const auto p=[](const Feature& f,unsigned i) { return fv(f.vertices[i].point); };
    const auto extreme=[&](F d,unsigned& ia,unsigned& ib) {
        ia=ib=0;float av=dxzy(p(a,0),d),bv=dxzy(p(b,0),d);
        for(unsigned i=1;i<a.count;++i) {
            const float v=dxzy(p(a,i),d);if(v>av){av=v;ia=i;}
        }
        for(unsigned i=1;i<b.count;++i) {
            const float v=dxzy(p(b,i),d);if(v<bv){bv=v;ib=i;}
        }
        return bv>av;
    };
    const auto apply=[&](const FeatureChoice& c) {
        Feature aa,bb;aa.count=c.na;bb.count=c.nb;
        for(unsigned i=0;i<c.na;++i)aa.vertices[i]=a.vertices[c.ia[i]];
        for(unsigned i=0;i<c.nb;++i)bb.vertices[i]=b.vertices[c.ib[i]];
        a=aa;b=bb;wa=dv(c.a);wb=dv(c.b);direction=dv(c.direction);squared=c.squared;
    };
    const auto candidate=[&](FeatureChoice c,bool early,const F* distance=nullptr) {
        const F delta=distance?*distance:fs(c.b,c.a);c.squared=fd(delta,delta);
        // On intersection46EA50 returns zero without rewriting witnesses.
        // Witnesses written by earlier separating candidates remain live.
        wa=dv(c.a);wb=dv(c.b);
        if(early){apply(c);return true;}
        if(c.squared<best.squared)best=c;
        return false;
    };
    const auto edge_indices=[](std::array<unsigned,3>& out,unsigned& count,
        unsigned i,unsigned j,float t) {
        count=(t==0.F||t==1.F)?1:2;out[0]=t==1.F?j:i;out[1]=j;
    };
    if(a.count==1&&b.count==1) {
        FeatureChoice c;c.a=p(a,0);c.b=p(b,0);c.direction=fs(c.b,c.a);c.na=c.nb=1;
        return candidate(c,true);
    }
    if(a.count==1||b.count==1) {
        const bool swap=b.count==1;const auto& edges=swap?a:b;
        const F point=p(swap?b:a,0);
        for(unsigned i=0;i+1<edges.count;++i)for(unsigned j=i+1;j<edges.count;++j) {
            const F origin=p(edges,i),edge=fs(p(edges,j),origin),r=fs(origin,point);
            const float length=dxzy(edge,edge);if(length==0)continue;
            const float t=clamp_feature(-dxzy(edge,r)/length);
            const F q=fa(origin,fm(edge,t));
            F d=(t==0.F||t==1.F)?fs(q,point):perpendicular(r,edge);
            if(swap)d=fm(d,-1.F);direction=dv(d);
            unsigned ai,bi;if(!extreme(d,ai,bi))continue;
            FeatureChoice c;c.a=swap?q:point;c.b=swap?point:q;c.direction=d;
            c.na=c.nb=1;
            edge_indices(swap?c.ia:c.ib,swap?c.na:c.nb,i,j,t);
            const unsigned selected=swap?ai:bi;
            if(candidate(c,selected==i||selected==j))return true;
        }
    } else {
        for(unsigned i=0;i+1<a.count;++i)for(unsigned j=i+1;j<a.count;++j) {
            const F origin=p(a,i),u=fs(p(a,j),origin);const float uu=fd(u,u);
            for(unsigned k=0;k+1<b.count;++k)for(unsigned l=k+1;l<b.count;++l) {
                const F other=p(b,k),v=fs(p(b,l),other),r=fs(other,origin);
                const float vv=dxzy(v,v),uv=dxzy(u,v),ur=dxzy(u,r),vr=dxzy(v,r);
                if(uu==0||vv==0)continue;
                const float determinant=uu*vv-uv*uv;
                float s=std::abs(determinant)>0.00001F?
                    clamp_feature((ur*vv-uv*vr)/determinant):0.F;
                float t=(uv*s-vr)/vv;
                if(t<=0.F||t>=1.F) {
                    t=t<=0.F?0.F:1.F;s=clamp_feature((uv*t+ur)/uu);
                }
                const F pa=fa(origin,fm(u,s)),pb=fa(other,fm(v,t));F d;
                const bool end_a=s==0.F||s==1.F,end_b=t==0.F||t==1.F;
                if(end_a&&end_b)d=fs(pb,pa);
                else if(end_b)d=perpendicular(fs(pb,origin),u);
                else if(end_a)d=perpendicular(fs(other,pa),v);
                else {
                    d=fu(fc(u,v));if(dyzx(r,d)<0)d=fm(d,-1.F);
                    const auto gap=[&](F n) {
                        return std::min(fd(other,n),fd(p(b,l),n))-
                            std::max(fd(origin,n),fd(p(a,j),n));
                    };
                    //46FD80..4700D4 select the largest edge separation.
                    for(F q:{perpendicular(fs(other,pa),v),perpendicular(fs(pb,origin),u)})
                        if(gap(q)>gap(d))d=q;
                }
                direction=dv(d);unsigned ai,bi;if(!extreme(d,ai,bi))continue;
                FeatureChoice c;c.a=pa;c.b=pb;c.direction=d;
                edge_indices(c.ia,c.na,i,j,s);edge_indices(c.ib,c.nb,k,l,t);
                if(candidate(c,(ai==i||ai==j)&&(bi==k||bi==l)))return true;
            }
        }
    }
    //46F0CF visits A faces first;47120A visits B faces second.
    for(bool swap:{false,true}) {
        const auto& face=swap?b:a;const auto& points=swap?a:b;
        for(unsigned i=0;i+2<face.count;++i)for(unsigned j=i+1;j+1<face.count;++j)
        for(unsigned k=j+1;k<face.count;++k) {
            const F origin=p(face,i),normal=fu(fc(fs(p(face,j),origin),fs(p(face,k),origin)),true);
            F d=normal;
            if(dxzy(swap?fs(origin,p(points,0)):fs(p(points,0),origin),d)<0)d=fm(d,-1.F);
            direction=dv(d);unsigned ai,bi;if(!extreme(d,ai,bi))continue;
            const unsigned selected=swap?ai:bi;const F q=p(points,selected);
            bool inside=true;
            for(const auto edge:{std::array<unsigned,2>{i,j},{j,k},{k,i}}) {
                if(fd(fc(normal,fs(p(face,edge[1]),p(face,edge[0]))),fs(q,p(face,edge[0])))<=0)
                    inside=false;
            }
            if(!inside)continue;
            const float plane=swap?dxyz(origin,d):fd(origin,d);
            const float height=(swap?dxzy(q,d):dyzx(q,d))-plane;
            const F distance=fm(d,height),projected=fs(q,distance);
            FeatureChoice c;c.a=swap?q:projected;c.b=swap?projected:q;c.direction=d;
            if(swap){c.na=1;c.nb=3;c.ia={selected,0,0};c.ib={i,j,k};}
            else {c.na=3;c.nb=1;c.ia={i,j,k};c.ib={selected,0,0};}
            if(candidate(c,(swap?bi:ai)==i,&distance))return true;
        }
    }
    if(best.na)apply(best);
    else squared=0; //471D46: preserve prior witnesses and feature lists.
    return true;
}

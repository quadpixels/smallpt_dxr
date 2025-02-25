RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
RWStructuredBuffer<uint> Scratch : register(u1);

struct SphereInfo
{
    float radius;
    float3 pos;
    float3 emission;
    float3 color;
    int material;
};
StructuredBuffer<SphereInfo> Spheres : register(t1);

cbuffer RayGenCB : register(b0)
{
    int frame_count; // start from 0
    float3 g_cam_pos;
    float3 g_cam_dir;
    int pad1;
    float3 g_cx;
    int pad2;
    float3 g_cy;
    int pad3;
    int g_recursion_depth;
    int g_nsamp;
}

struct MyAttributes
{
    float3 n;
};

struct RayPayload
{
    float thit;
    int pidx;
};

uint TausStep(inout uint z, int S1, int S2, int S3, uint M)
{
  uint b = (((z << S1) ^ z) >> S2);
  return z = (((z & M) << S3) ^ b);
}

uint LCGStep(inout uint z, uint A, uint C)
{
  return z = (A * z + C);
}

float HybridTaus(inout uint4 seed)
{
  // Combined period is lcm(p1,p2,p3,p4)~ 2^121
  return 2.3283064365387e-10 * ( // Periods
    TausStep(seed.x, 13, 19, 12, 4294967294UL) ^ // p1=2^31-1
    TausStep(seed.y, 2, 25, 4, 4294967288UL) ^ // p2=2^30-1
    TausStep(seed.z, 3, 11, 17, 4294967280UL) ^ // p3=2^28-1
    LCGStep(seed.w, 1664525, 1013904223UL) // p4=2^32
  );
}

float3 TraceRadiance(RayDesc ray, inout uint4 seed)
{
    float3 cl = { 0, 0, 0 };
    float3 cf = { 1, 1, 1 };
    int depth = 0;
    float seed1 = seed.x + seed.y * 720;
    
    while (true)
    {
        RayPayload payload;
        payload.pidx = -1;
        TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);
        if (payload.pidx == -1)
            return cl;
        
        SphereInfo si = Spheres[payload.pidx];
        float3 x = ray.Origin + ray.Direction * payload.thit;
        float3 n = normalize(x - si.pos);
        float3 nl = dot(n, ray.Direction) < 0 ? n : n * -1;
        float3 f = si.color;
        float p = f.x > f.y && f.x > f.z ? f.x : f.y > f.z ? f.y : f.z;
        cl = cl + cf * si.emission;
        
        if (++depth > g_recursion_depth)
        {
            if (depth < g_recursion_depth && HybridTaus(seed) < p)
            {
                f = f * (1 / p);
            }
            else
            {
                return cl;
            }
        }
        
        cf = cf * f;
        if (si.material == 0)  // Diffuse
        {
            float r1 = 2 * 3.14159 * HybridTaus(seed);
            float r2 = HybridTaus(seed), r2s = sqrt(r2);
            float3 w = nl, u = normalize(cross((abs(w.x) > 0.1 ? float3(0, 1, 0) : float3(1, 0, 0)), w));
            float3 v = cross(w, u);
            float3 d = normalize(u * cos(r1) * r2s + v * sin(r1) * r2s + w * sqrt(1 - r2));
            ray.Origin = x + nl * 0.02f;
            ray.Direction = d;
            ray.TMax = 100000;
            ray.TMin = 0;
            continue;
        }
        else if (si.material == 1)  // Specular reflection
        {
            ray.Origin = x + nl * 0.02f;
            ray.Direction = ray.Direction - n * 2 * dot(n, ray.Direction);
            continue;
        }
        else if (si.material == 2)  // Ideal dielectric REFRACTION
        {
            bool into = (dot(n, nl) > 0);
            float nc = 1, nt = 1.5f, nnt = into ? nc / nt : nt / nc;
            float ddn = dot(ray.Direction, nl);
            float cos2t = 1 - nnt * nnt * (1 - ddn * ddn);
            if (cos2t < 0)
            {
                ray.Origin = x + nl * 0.02f;
                ray.Direction = ray.Direction - n * 2 * dot(n, ray.Direction);
                continue;
            }
            float3 tdir = normalize(ray.Direction * nnt - n * ((into ? 1 : -1) * (ddn * nnt + sqrt(cos2t))));
            float a = nt - nc, b = nt + nc, R0 = a * a / (b * b), c = 1 - (into ? -ddn : dot(tdir, n));
            float Re = R0 + (1 - R0) * c * c * c * c * c;
            float Tr = 1 - Re;
            float P = 0.25 + 0.5 * Re;
            float RP = Re / P;
            float TP = Tr / (1 - P);
            if (HybridTaus(seed) < P)
            {
                cf = cf * RP;
                ray.Origin = x + nl * 0.01f;
                ray.Direction = ray.Direction - n * 2 * dot(n, ray.Direction);
            }
            else
            {
                cf = cf * TP;
                ray.Origin = x - nl * 0.01f;
                ray.Direction = tdir;
            }
            continue;
        }
    }
}

[shader("raygeneration")]
void RayGen()
{
    uint2 tid = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    
    float3 cam = g_cam_pos;
    float3 dir = g_cam_dir;
    float3 cx = g_cx;
    float3 cy = g_cy;

    // rng
    uint4 seed;
    uint s1;
    if (frame_count == 0)
        s1 = tid.x + tid.y * dim.x;
    else
        s1 = Scratch[tid.x + tid.y * dim.x];

    LCGStep(s1, 1664525, 1013904223UL);
    seed.x = s1;
    LCGStep(s1, 1664525, 1013904223UL);
    seed.y = s1;
    LCGStep(s1, 1664525, 1013904223UL);
    seed.z = s1;
    LCGStep(s1, 1664525, 1013904223UL);
    seed.w = s1;

    for (int n = 0; n < g_nsamp; n++)
    {
        float r1 = 2 * HybridTaus(seed), dx = (r1 < 1) ? sqrt(r1) - 1 : 1 - sqrt(2 - r1);
        float r2 = 2 * HybridTaus(seed), dy = (r2 < 1) ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);

        float3 ray_d = cx * ((tid.x + 0.5f + dx) / dim.x - 0.5f) +
                       cy * ((tid.y + 0.5f + dy) / dim.y - 0.5f) * (-1) + dir;
        RayDesc ray;
        ray.Origin = cam;
        ray.Direction = normalize(ray_d);
        ray.TMin = 0.0f;
        ray.TMax = 1e20f;
        float3 rad = TraceRadiance(ray, seed);
    
        RenderTarget[tid] += float4(rad, 1);

        s1 = seed.x ^ seed.y ^ seed.z ^ seed.w ^ (frame_count + n);
    }
    
    Scratch[tid.x + tid.y * dim.x] = s1;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in MyAttributes attr)
{
    int pidx = PrimitiveIndex();
    payload.pidx = pidx;
    payload.thit = RayTCurrent();
}

// https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection.html
bool solveQuadratic(float a, float b, float c,
					inout float x0, inout float x1)
{
    float discr = b * b - 4 * a * c;
    if (discr < 0)
        return false;
    else if (discr == 0)
        x0 = x1 = -0.5 * b / a;
    else
    {
        float q = (b > 0) ?
            -0.5 * (b + sqrt(discr)) :
            -0.5 * (b - sqrt(discr));
        x0 = q / a;
        x1 = c / q;
    }
    if (x0 > x1)
    {
        float tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    return true;
}

[shader("intersection")]
void Intersection()
{
    float3 o = WorldRayOrigin();
    float3 d = WorldRayDirection();
    float tmin = RayTMin();
    float tmax = RayTCurrent();
    MyAttributes attr;
    
    SphereInfo si = Spheres[PrimitiveIndex()];
    
    float t0, t1;
     // Analytic solution
    float3 L = o - si.pos;
    float a = dot(d, d);
    float b = 2 * dot(d, L);
    float c = dot(L, L) - si.radius * si.radius;
    
    if (!solveQuadratic(a, b, c, t0, t1))
        return;
    else
    {
        if (t0 > tmin && t0 < tmax)
        {
            float3 p = o + d * t0;
            ReportHit(t0, 0, attr);
        }
        else if (t1 > tmin && t1 < tmax)
        {
            float3 p = o + d * t1;
            ReportHit(t1, 0, attr);
        }
        else
            return;

    }
}
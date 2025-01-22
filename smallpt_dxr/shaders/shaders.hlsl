RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);

struct SphereInfo
{
    float radius;
    float3 pos;
};
StructuredBuffer<SphereInfo> Spheres : register(t1);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    float4 color;
};

[shader("raygeneration")]
void RayGen()
{
    uint2 tid = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    
    float3 cam = { 50, 52, 295.6 };
    float3 dir = normalize(float3(0, -0.042612, -1));
    float3 cx = { dim.x * 0.5135f / dim.y, 0, 0 };
    float3 cy = normalize(cross(cx, dir)) * 0.5135f;

    float3 ray_d = cx * ((tid.x + 0.5f) / dim.x - 0.5f) +
                   cy * ((tid.y + 0.5f) / dim.y - 0.5f) * (-1) + dir;
    RayDesc ray;
    ray.Origin = cam + ray_d * 140.0f;
    ray.Direction = normalize(ray_d);
    ray.TMin = 0.0f;
    ray.TMax = 10000.0f;
    RayPayload payload;
    TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 1, 0, ray, payload);
    
    RenderTarget[tid] = payload.color;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in MyAttributes attr)
{
    int pidx = PrimitiveIndex();
    switch (pidx)
    {
        case 0:
            payload.color = float4(1, 0, 0, 1);
            break;
        case 1:
            payload.color = float4(0, 1, 0, 1);
            break;
        case 2:
            payload.color = float4(0, 0, 1, 1);
            break;
        case 3:
            payload.color = float4(1, 1, 0, 1);
            break;
        case 4:
            payload.color = float4(1, 0, 1, 1);
            break;
        case 5:
            payload.color = float4(0, 1, 1, 1);
            break;
        case 6:
            payload.color = float4(0, 0.5, 1, 1);
            break;
        case 7:
            payload.color = float4(0, 1, 0.5, 1);
            break;
        case 8:
            payload.color = float4(0.5, 1, 1, 1);
            break;
        default:
            payload.color = float4(0, 0, 0, 0);
            break;
    }
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
            ReportHit(t0, 0, attr);
        }
        else if (t1 > tmin && t1 < tmax)
        {
            ReportHit(t1, 0, attr);
        }
        else
            return;

    }
}
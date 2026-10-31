// Windows GPU load: a D3D11 compute shader on the NVIDIA adapter for N seconds.
// Each dispatch is sized to about 20 ms, far below the 2 s GPU timeout. Prints throughput per second.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

static const char kShader[] = R"(
RWStructuredBuffer<uint> out_buf : register(u0);
[numthreads(32, 32, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint x = id.x * 2654435761u + id.y;
  for (uint i = 0; i < 200; ++i) x = (x ^ (x >> 7)) * 2654435761u + i;
  out_buf[(id.x + id.y) & 1023] = x;
}
)";

static double now_s() {
  static LARGE_INTEGER f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return v; }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? atoi(argv[1]) : 10;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return 1;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d;
    adapter->GetDesc1(&d);
    if (d.VendorId == 0x10DE) { printf("adapter %ls\n", d.Description); break; }
    adapter->Release();
    adapter = nullptr;
  }
  if (!adapter) { puts("no NVIDIA adapter"); return 1; }

  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  const D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) return 1;

  ID3DBlob* code = nullptr;
  ID3DBlob* err = nullptr;
  if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &err))) {
    puts(err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed");
    return 1;
  }
  ID3D11ComputeShader* cs = nullptr;
  if (FAILED(dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &cs))) return 1;

  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = 4 * 1024;
  bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  bd.StructureByteStride = 4;
  ID3D11Buffer* buf = nullptr;
  if (FAILED(dev->CreateBuffer(&bd, nullptr, &buf))) return 1;
  D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
  ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  ud.Buffer.NumElements = 1024;
  ID3D11UnorderedAccessView* uav = nullptr;
  if (FAILED(dev->CreateUnorderedAccessView(buf, &ud, &uav))) return 1;

  D3D11_QUERY_DESC qd{};
  qd.Query = D3D11_QUERY_EVENT;
  ID3D11Query* q = nullptr;
  if (FAILED(dev->CreateQuery(&qd, &q))) return 1;
  ctx->CSSetShader(cs, nullptr, 0);
  ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

  const auto run = [&](UINT groups) {
    const double t = now_s();
    ctx->Dispatch(groups, groups, 1);
    ctx->End(q);
    ctx->Flush();
    BOOL done = FALSE;
    while (ctx->GetData(q, &done, sizeof done, 0) != S_OK) Sleep(0);
    return now_s() - t;
  };

  UINT groups = 16;
  double took = run(groups);
  while (took < 0.020 && groups < 4096) {
    groups = groups * 5 / 4 + 1;
    took = run(groups);
  }
  printf("calibrated groups=%u dispatch_ms=%.1f\n", groups, took * 1000);
  fflush(stdout);

  const double start = now_s();
  for (int sec = 1; sec <= seconds; ++sec) {
    int n = 0;
    double sum = 0, worst = 0;
    while (now_s() - start < sec) {
      const double t = run(groups);
      sum += t;
      if (t > worst) worst = t;
      ++n;
    }
    printf("sec=%d dispatches=%d mean_ms=%.1f worst_ms=%.1f\n", sec, n, n ? sum / n * 1000 : 0, worst * 1000);
    fflush(stdout);
  }
  return 0;
}

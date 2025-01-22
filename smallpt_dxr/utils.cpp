#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxcapi.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxgi.lib")

// CE = Check Error
#define CE(call) \
{ \
  HRESULT x = (call); \
  if (FAILED(x)) \
  { \
    printf("ERROR: %X line %d\n", x, __LINE__); \
    throw std::exception(); \
  } \
}

IDxcBlob* CompileShaderLibrary(LPCWSTR fileName)
{
  static IDxcCompiler* pCompiler = nullptr;
  static IDxcLibrary* pLibrary = nullptr;
  static IDxcIncludeHandler* dxcIncludeHandler;

  HRESULT hr;

  // Initialize the DXC compiler and compiler helper
  if (!pCompiler)
  {
    CE(DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&pCompiler)));
    CE(DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&pLibrary)));
    CE(pLibrary->CreateIncludeHandler(&dxcIncludeHandler));
  }
  // Open and read the file
  std::ifstream shaderFile(fileName);
  if (shaderFile.good() == false)
  {
    throw std::logic_error("Cannot find shader file");
  }
  std::stringstream strStream;
  strStream << shaderFile.rdbuf();
  std::string sShader = strStream.str();

  // Create blob from the string
  IDxcBlobEncoding* pTextBlob;
  CE(pLibrary->CreateBlobWithEncodingFromPinned(LPBYTE(sShader.c_str()), static_cast<uint32_t>(sShader.size()), 0, &pTextBlob));

  // Compile
  IDxcOperationResult* pResult;
  const wchar_t* args[] = { L"-O3" };
  CE(pCompiler->Compile(pTextBlob, fileName, L"", L"lib_6_5", args, 1, nullptr, 0, dxcIncludeHandler, &pResult));

  // Verify the result
  HRESULT resultCode;
  CE(pResult->GetStatus(&resultCode));
  if (FAILED(resultCode))
  {
    IDxcBlobEncoding* pError;
    hr = pResult->GetErrorBuffer(&pError);
    if (FAILED(hr))
    {
      throw std::logic_error("Failed to get shader compiler error");
    }

    // Convert error blob to a string
    std::vector<char> infoLog(pError->GetBufferSize() + 1);
    memcpy(infoLog.data(), pError->GetBufferPointer(), pError->GetBufferSize());
    infoLog[pError->GetBufferSize()] = 0;

    std::string errorMsg = "Shader Compiler Error:\n";
    errorMsg.append(infoLog.data());

    MessageBoxA(nullptr, errorMsg.c_str(), "Error!", MB_OK);
    throw std::logic_error("Failed compile shader");
  }

  IDxcBlob* pBlob;
  CE(pResult->GetResult(&pBlob));
  return pBlob;
}
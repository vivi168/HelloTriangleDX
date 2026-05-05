#pragma once

inline std::filesystem::path GetExecutableDirectory()
{
#if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
  wchar_t moduleName[_MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, moduleName, _MAX_PATH))
      throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()),
                              "GetModuleFileNameW");
  return std::filesystem::path(moduleName).parent_path();
#else
  // TODO: sorry
  return std::filesystem::current_path();
#endif
}

inline std::vector<uint8_t> ReadData(std::filesystem::path file)
{
  std::ifstream inFile(file, std::ios::in | std::ios::binary | std::ios::ate);

  if (!inFile && file.is_relative()) {
    inFile.open(GetExecutableDirectory() / file, std::ios::in | std::ios::binary | std::ios::ate);
  }

  if (!inFile) throw std::runtime_error("Read ShaderModule");

  const std::streampos len = inFile.tellg();
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  std::vector<uint8_t> v;
  v.resize(size_t(len));

  inFile.seekg(0, std::ios::beg);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.read(reinterpret_cast<char*>(v.data()), len);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.close();

  return v;
}

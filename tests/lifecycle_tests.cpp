#include "session.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace banshou;
int main(){auto mock=std::filesystem::path(exePath()).parent_path()/L"mock_target.exe";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};std::wstring command=L"\""+mock.wstring()+L"\"";assert(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi));CloseHandle(pi.hThread);HWND window=nullptr;for(int i=0;i<50&&!window;++i){Sleep(20);window=FindWindowW(L"BanshouMockTarget",L"Banshou Mock Target");}assert(window);SessionController controller;std::string last;controller.onState=[&](SessionState, uint64_t,const std::string&e){last=e;};for(int i=0;i<1000;++i){controller.attach(window,-1);if(!controller.waitFor(SessionState::Attached)){std::cerr<<"attach failed: "<<last<<"\n";TerminateProcess(pi.hProcess,0);CloseHandle(pi.hProcess);return 2;}controller.detach();controller.detach();assert(controller.waitFor(SessionState::Detached));}TerminateProcess(pi.hProcess,0);CloseHandle(pi.hProcess);return 0;}

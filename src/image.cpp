#include "image.hpp"
#include <capstone/capstone.h>
#include <algorithm>
#include <sstream>
namespace banshou {
std::vector<size_t> disassembleX86(const Bytes& code,uint32_t address){csh handle=0;if(cs_open(CS_ARCH_X86,CS_MODE_32,&handle)!=CS_ERR_OK)throw Error("Capstone 初始化失败");cs_option(handle,CS_OPT_DETAIL,CS_OPT_OFF);cs_insn* instructions=nullptr;auto count=cs_disasm(handle,code.data(),code.size(),address,0,&instructions);std::vector<size_t> lengths;lengths.reserve(count);for(size_t i=0;i<count;++i)lengths.push_back(instructions[i].size);if(instructions)cs_free(instructions,count);cs_close(&handle);return lengths;}
MaskedSignature MaskedSignature::parse(const std::string& text) {
    MaskedSignature out; std::istringstream stream(text); std::string token;
    while (stream >> token) { if (token=="??" || token=="?") { out.bytes.push_back(0); out.mask.push_back(0); } else { if(token.size()!=2) throw Error("Invalid signature token"); out.bytes.push_back(unhex(token).at(0)); out.mask.push_back(255); } }
    if(out.bytes.empty()) throw Error("Empty signature"); return out;
}
std::vector<uint32_t> MaskedSignature::findAll(const Bytes& data,uint32_t base) const {
    std::vector<uint32_t> out; if(bytes.empty()||mask.size()!=bytes.size())throw Error("Invalid signature"); if(bytes.size()>data.size())return out;
    size_t anchor=0; while(anchor<mask.size()&&!mask[anchor])++anchor; size_t last=data.size()-bytes.size();
    for(size_t i=0;i<=last;++i){if(anchor<mask.size()&&data[i+anchor]!=bytes[anchor])continue;bool ok=true;for(size_t j=0;j<bytes.size();++j)if(mask[j]&&data[i+j]!=bytes[j]){ok=false;break;}if(ok)out.push_back(checkedAddress(base,i));}return out;
}
PEImage::PEImage(const std::wstring& path):image_(readFile(path)){
    auto n=[&](size_t o,size_t w){return uint32_t(number(image_,o,w));};
    if(n(0,2)!=0x5a4d)throw Error("Invalid DOS signature");auto pe=size_t(n(0x3c,4));if(pe>image_.size()||image_.size()-pe<24||n(pe,4)!=0x4550)throw Error("Invalid PE signature");
    machine_=uint16_t(n(pe+4,2));auto count=n(pe+6,2),optional=n(pe+20,2),opt=pe+24;if(machine_!=IMAGE_FILE_MACHINE_I386||optional<96||opt>image_.size()||optional>image_.size()-opt||n(opt,2)!=0x10b)throw Error("目标主模块不是 x86 PE32 程序");
    imageBase_=n(opt+28,4);imageSize_=n(opt+56,4);headerSize_=n(opt+60,4);checkedAddress(imageBase_,0,imageSize_);if(headerSize_>image_.size()||headerSize_>imageSize_)throw Error("Invalid PE header size");
    auto sh=opt+optional;if(!count||count>96||sh>image_.size()||size_t(count)*40>image_.size()-sh)throw Error("Truncated PE sections");
    for(unsigned i=0;i<count;++i){auto p=sh+i*40;size_t ns=0;while(ns<8&&image_[p+ns])++ns;PESection s;s.name=std::string(image_.begin()+p,image_.begin()+p+ns);s.va=n(p+12,4);s.virtualSize=n(p+8,4);s.raw=n(p+20,4);s.rawSize=n(p+16,4);s.characteristics=n(p+36,4);if(s.raw>image_.size()||s.rawSize>image_.size()-s.raw)throw Error("Truncated PE raw section");auto extent=s.virtualSize?s.virtualSize:s.rawSize;if(s.va>imageSize_||extent>imageSize_-s.va)throw Error("PE section virtual range outside mapped image");sections_.push_back(std::move(s));}
}
bool PEImage::isX86()const{return machine_==IMAGE_FILE_MACHINE_I386;}
uint32_t PEImage::vaToOffset(uint32_t va)const{if(va<imageBase_)throw Error("VA below image base");auto r=va-imageBase_;if(r<headerSize_)return r;for(auto&s:sections_)if(r>=s.va&&uint64_t(r)<uint64_t(s.va)+s.rawSize)return s.raw+r-s.va;throw Error("VA has no file backing");}
Bytes PEImage::readVA(uint32_t va,size_t size)const{if(!size)throw Error("Empty VA read");auto o=vaToOffset(va);auto e=vaToOffset(checkedAddress(va,size-1));if(e!=o+size-1||size>image_.size()-o)throw Error("VA read crosses unmapped data");return Bytes(image_.begin()+o,image_.begin()+o+size);}
Bytes PEImage::sectionData(const PESection&s)const{return Bytes(image_.begin()+s.raw,image_.begin()+s.raw+s.rawSize);}
std::vector<uint32_t> PEImage::findSignature(const MaskedSignature&sig,bool executableOnly)const{std::vector<uint32_t>out;for(auto&s:sections_){if(cancelled&&cancelled())throw Error("扫描已取消",ERROR_CANCELLED);if(executableOnly&&!s.executable())continue;auto h=sig.findAll(sectionData(s),checkedAddress(imageBase_,s.va));out.insert(out.end(),h.begin(),h.end());}return out;}
std::vector<uint32_t> PEImage::findLiteralReferences(uint32_t value,bool executableOnly)const{Bytes needle{uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};std::vector<uint32_t>out;for(auto&s:sections_){if(cancelled&&cancelled())throw Error("扫描已取消",ERROR_CANCELLED);if(executableOnly&&!s.executable())continue;auto data=sectionData(s);if(data.size()<needle.size())continue;size_t start=0;while(start+needle.size()<=data.size()){auto it=std::search(data.begin()+start,data.end(),needle.begin(),needle.end());if(it==data.end())break;auto index=size_t(it-data.begin());out.push_back(checkedAddress(imageBase_,uint64_t(s.va)+index,needle.size()));start=index+1;}}return out;}
RuntimeModuleImage::RuntimeModuleImage(const std::wstring&path,std::shared_ptr<MemorySession>s):PEImage(path),session_(std::move(s)){if(!session_)throw Error("Runtime image requires session");session_->referenceBase=imageBase_;if(session_->moduleSize!=imageSize_)throw Error("磁盘与运行时主模块大小不一致");auto h=session_->readActual(session_->moduleBase,headerSize_);auto pe=number(h,0x3c,4);if(number(h,0,2)!=0x5a4d||number(h,size_t(pe),4)!=0x4550||number(h,size_t(pe)+4,2)!=IMAGE_FILE_MACHINE_I386)throw Error("运行时 PE 身份无效");}
Bytes RuntimeModuleImage::readVA(uint32_t va,size_t size)const{return session_->read(va,size);}
Bytes RuntimeModuleImage::sectionData(const PESection&s)const{auto it=mapped_.find(s.va);if(it==mapped_.end()){auto len=s.virtualSize?s.virtualSize:s.rawSize;Bytes d;d.reserve(len);for(uint32_t off=0;off<len;){if(cancelled&&cancelled())throw Error("扫描已取消",ERROR_CANCELLED);auto take=std::min<uint32_t>(65536,len-off);auto p=readVA(checkedAddress(imageBase_,uint64_t(s.va)+off,take),take);d.insert(d.end(),p.begin(),p.end());off+=take;}it=mapped_.emplace(s.va,std::move(d)).first;}return it->second;}
uint32_t RuntimeModuleImage::operandReference(uint32_t operand)const{if(operand<session_->moduleBase||uint64_t(operand)>=uint64_t(session_->moduleBase)+session_->moduleSize)return operand;return checkedAddress(imageBase_,operand-session_->moduleBase);}
}

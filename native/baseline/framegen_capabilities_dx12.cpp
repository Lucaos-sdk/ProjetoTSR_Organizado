#include "dx12_context.h"
#include "../../_OptiScaler_Source/external/xess/inc/xess_fg/xefg_swapchain_d3d12.h"
#include <filesystem>
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: tsr_framegen_capabilities_dx12 absolute-libxess_fg.dll");
        auto path=std::filesystem::absolute(argv[1]);
        HMODULE dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!dll)throw std::runtime_error("Library load failed: "+std::to_string(GetLastError()));
        auto create=reinterpret_cast<decltype(&xefgSwapChainD3D12CreateContext)>(GetProcAddress(dll,"xefgSwapChainD3D12CreateContext"));
        auto properties=reinterpret_cast<decltype(&xefgSwapChainGetProperties)>(GetProcAddress(dll,"xefgSwapChainGetProperties"));
        auto destroy=reinterpret_cast<decltype(&xefgSwapChainDestroy)>(GetProcAddress(dll,"xefgSwapChainDestroy"));
        if(!create || !properties || !destroy)throw std::runtime_error("Required exports missing");
        Context c(false,false,true);xefg_swapchain_handle_t handle=nullptr;
        const auto result=create(c.device.Get(),&handle);
        std::cout<<"CreateContext result="<<int(result)<<'\n';
        if(result!=XEFG_SWAPCHAIN_RESULT_SUCCESS)throw std::runtime_error("XeFG unavailable on this device/runtime");
        xefg_swapchain_properties_t props{};const auto queried=properties(handle,&props);
        const auto destroyed=destroy(handle);
        if(queried!=XEFG_SWAPCHAIN_RESULT_SUCCESS || destroyed!=XEFG_SWAPCHAIN_RESULT_SUCCESS)throw std::runtime_error("Properties/destroy failed");
        std::cout<<"maxSupportedInterpolations="<<props.maxSupportedInterpolations<<" maximum_total_multiplier="<<props.maxSupportedInterpolations+1
                 <<" supports_MFG="<<(props.maxSupportedInterpolations>1?"true":"false")<<'\n';
        std::cout<<"PASS capability query only; interpolation/presentation not tested\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

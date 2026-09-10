#include "texture_region.h"
#include <iostream>
int main() {
    tsr::ValidateRegion({20,10},{17,7},3,3);
    tsr::ValidateRegion({1,1},{1,1},0,0);
    unsigned rejected=0;
    for(auto x:{4u,21u,UINT32_MAX}) {
        try {tsr::ValidateRegion({20,10},{17,7},x,3);}catch(const std::invalid_argument&){++rejected;}
    }
    try {tsr::ValidateRegion({20,10},{17,7},3,4);}catch(const std::invalid_argument&){++rejected;}
    try {tsr::ValidateRegion({20,10},{0,7},0,0);}catch(const std::invalid_argument&){++rejected;}
    if(rejected!=5)throw std::runtime_error("Region bounds accepted invalid extent/origin");
    std::cout<<"PASS region edges and unsigned overflow rejection\n";
}

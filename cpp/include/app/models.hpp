#pragma once
#include <string>

namespace app {

struct ConfigModel {
    std::string devicename;
    std::string mode;
    std::string wifissid;
    std::string wifipassword;
    bool provisioningenabled{false};
    bool cameraconnected{false};
};

}

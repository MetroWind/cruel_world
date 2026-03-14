#include <iostream>

#include "app.hpp"

int main(int argc, char** argv)
{
    std::string config_path = "config.yaml";
    if(argc > 1)
    {
        config_path = argv[1];
    }

    auto config_res = config::loadConfig(config_path);
    if(!config_res.has_value())
    {
        std::cerr << "Failed to load config: "
                  << mw::errorMsg(config_res.error()) << std::endl;
        return 1;
    }

    auto db_res = Database::open(config_res.value().data_dir + "/cruel_world.db");
    if(!db_res.has_value())
    {
        std::cerr << "Failed to open database: " << mw::errorMsg(db_res.error())
                  << std::endl;
        return 1;
    }

    auto init_res = db_res.value().initSchema();
    if(!init_res.has_value())
    {
        std::cerr << "Failed to init schema: " << mw::errorMsg(init_res.error())
                  << std::endl;
        return 1;
    }

    JournalApp app(config_res.value(), std::move(db_res));

    std::cout << "Starting Cruel World server on "
              << config_res.value().bind_address << ":"
              << config_res.value().bind_port << std::endl;
    auto err = app.start();
    if(!err.has_value())
    {
        std::cerr << "Failed to start server: " << mw::errorMsg(err.error())
                  << std::endl;
        return 1;
    }
    app.wait();

    return 0;
}

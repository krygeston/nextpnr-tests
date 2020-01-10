#include "gtest/gtest.h"
#include "nextpnr.h"
#include "timing.h"
#include "bitstream.h"
#include <iostream>
#include <fstream>
#include <QDir>
#include "log.h"
#include "worker.h"
#include <vector>
#include "ice40commandhandler.h"
#include <string>

USING_NEXTPNR_NAMESPACE

const QDir dir("tests/ice40/load_test_files");
void compare_ctx_objects(QString file, char **args, int argv);


TEST(LOAD_Test, parser){

    std::vector<QString> failure_files = {"no_device.asc", "multiply_devices.asc", "unknown_device_device.asc", "device_after_tile.asc",
                                          "missing_coord.asc", "neg_coord.asc", "too_large_coord.asc", "wrong_symbol_tile_bit_row.asc",
                                          "too_short_tile_bit_row.asc", "too_large_tile_bit_row.asc", "extra_tile_bit_row.asc",
                                          "missing_tile_bit_row.asc", "missing_wire_id.asc", "missing_net_name.asc"};
    std::ifstream in(dir.filePath("blinky.asc").toStdString());
    ASSERT_NO_THROW(read_asc(in));

    for(QString file :failure_files){
        std::ifstream in(dir.absoluteFilePath(file).toStdString());
        ASSERT_THROW(read_asc(in), log_execution_error_exception)  << "failed file: "  << file.toStdString();
    }
    {
        ArchArgs chip;
        chip.type = ArchArgs::UP5K;
        chip.package = "sg48";
        std::ifstream in(dir.absoluteFilePath("blinky.asc").toStdString());
        std::unique_ptr<Context> ctx = std::unique_ptr<Context>(new Context(chip));
        ASSERT_FALSE(read_asc(ctx.get(), in));
    }
    {
        ArchArgs chip;
        chip.type = ArchArgs::HX1K;
        chip.package = "tq144";
        std::ifstream in(dir.absoluteFilePath("blinky.asc").toStdString());
        std::unique_ptr<Context> ctx = std::unique_ptr<Context>(new Context(chip));
        ASSERT_TRUE(read_asc(ctx.get(), in));
    }
}

TEST(LOAD_Test, load_write_test){

    //TODO file "uart_transmission.json" generates an asc file with a net which has a net
    //"uart_txbyte_SB_LUT4_O_I3_SB_LUT4_I2_O_SB_LUT4_I2_O" with 2 drivers
    //error not reproducable per gui or commandline only in this test
    std::vector<QString> test_files_hx1k = {"blinky.json", "fsm_simple.json",
                                            "buttons_nopullup.json", "buttons_debounce.json", "buttons_bounce.json"
                                           };



    for(QString file : test_files_hx1k){
        char init[] = "nextpnr_ice40";
        char arch[] = "--hx1k";
        char pack1[] = "--package";
        char pack2[] = "tq144";
        char *args[4];
        args[0] = init;
        args[1] = arch;
        args[2] = pack1;
        args[3] = pack2;

        compare_ctx_objects(file, args, 4);
    }


}

void compare_ctx_objects(QString file, char **args, int argv){

    Ice40CommandHandler handler(argv, args);
    std::unique_ptr<Context> ctx_norm;
    ASSERT_NO_THROW(ctx_norm = handler.load_json(dir.absoluteFilePath(file).toStdString());)
            << "file: " << file.toStdString() << "\n";

    ctx_norm->pack();
    assign_budget(ctx_norm.get());
    ctx_norm->check();
    ctx_norm->place();
    ctx_norm->check();
    ctx_norm->route();

    std::stringstream norm_write_out;

    ASSERT_NO_THROW(write_asc(ctx_norm.get(), norm_write_out)) << "file: " << file.toStdString() << "\n";
    std::unique_ptr<Context> ctx_load;
    ASSERT_NO_THROW(ctx_load = read_asc(norm_write_out)) << "file: " << file.toStdString() << "\n";


    if(ctx_norm->archArgs().type == ArchArgs::HX1K || ctx_norm->archArgs().type == ArchArgs::LP1K){
        ASSERT_TRUE(ctx_load->archArgs().type == ArchArgs::HX1K || ctx_load->archArgs().type == ArchArgs::LP1K)
                << "file: " << file.toStdString() << "\n" << "unequal chip types";
    }else if(ctx_norm->archArgs().type == ArchArgs::HX8K || ctx_norm->archArgs().type == ArchArgs::LP8K){
        ASSERT_TRUE(ctx_load->archArgs().type == ArchArgs::HX8K || ctx_load->archArgs().type == ArchArgs::LP8K)
                << "file: " << file.toStdString() << "\n" << "unequal chip types";
    }else {
        ASSERT_EQ(ctx_norm->archArgs().type, ctx_load->archArgs().type)
                << "file: " << file.toStdString() << "\n" << "unequal chip types";
    }

    size_t nets_not_restorable = 0;
    for(auto &net : ctx_norm->nets){
        if(net.second){
            if(net.second->wires.empty()){
                nets_not_restorable++;
            }
        }
    }

    EXPECT_EQ(ctx_norm->nets.size() - nets_not_restorable, ctx_load->nets.size())
            << "file: " << file.toStdString() << "\n" << "nets_not_restorable: " << nets_not_restorable;

    for(auto &net_ref : ctx_norm->nets){
        auto wire_ref = net_ref.second->wires.begin();
        NetInfo *net_load;
        if(wire_ref != net_ref.second->wires.end()){
            //lutff_y/in_x_lut wires are not always reconstrable therefore unusable to identify nets
            bool goto_next_net = false;
            while(ctx_norm->chip_info->wire_data[wire_ref->first.index].type == WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT){
                wire_ref++;
                if(wire_ref == net_ref.second->wires.end()){
                    goto_next_net = true;
                    break;
                }
            }
            if(goto_next_net)
                continue;

            //Test if reference and load ctx have the same wires + pips in the same nets
            net_load = ctx_load->wire_to_net[wire_ref->first.index];
            ASSERT_TRUE(net_load) << "file: " << file.toStdString() << "\n"
                                  << "a net is not generated by the load function\n"
                                  << "Missing reference net: " <<  net_ref.first.str(ctx_norm.get())
                                  << "\nReference wire " << ctx_norm->getWireName(wire_ref->first).c_str(ctx_norm.get());
            for(; wire_ref != net_ref.second->wires.end(); wire_ref++){
                if(ctx_norm->chip_info->wire_data[wire_ref->first.index].type == WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT)
                    continue;
                auto find = net_load->wires.find(wire_ref->first);
                ASSERT_NE(find, net_load->wires.end()) << "file: " << file.toStdString() << "\n"
                                                       << "Could not find wire in net\nMissing Wire: "
                                                       << ctx_norm->getWireName(wire_ref->first).c_str(ctx_norm.get())
                                                       << "\nReference net: " << net_ref.first.str(ctx_norm.get())
                                                       << "\nLoad net: " << net_load->name.str(ctx_load.get());
                if(wire_ref->second.pip != PipId()){
                    int x = -1,y = -1;
                    for(auto attr : ctx_norm->getWireAttrs(wire_ref->first)){
                        if(attr.first == ctx_norm->id("GRID_X"))
                            x = std::stoi(attr.second);
                        if(attr.first == ctx_norm->id("GRID_Y"))
                            y = std::stoi(attr.second);
                    }

                    if(ctx_norm->chip_info->wire_data[wire_ref->first.index].type == WireInfoPOD::WIRE_TYPE_LUTFF_OUT
                            && ctx_norm->chip_info->tile_grid[y * ctx_norm->chip_info->width + x] == TileType::TILE_LOGIC){
                        int pips = 0;
                        for(const auto &item : ctx_load->getPipsUphill(wire_ref->first)){
                            if(ctx_load->pip_to_net[item.index] != nullptr)
                                pips++;
                        }
                        ASSERT_TRUE(pips == 1) << "file: " << file.toStdString() << "\n"
                                               << "lutff pip failure";
                    }else{
                        ASSERT_EQ(wire_ref->second.pip.index, find->second.pip.index)
                                << "file: " << file.toStdString() << "\n"
                                << "pip reference: " << ctx_norm->getPipName(wire_ref->second.pip).str(ctx_norm.get());
                    }
                }else{
                    ASSERT_TRUE(find->second.pip == PipId())
                            << "file: " << file.toStdString() << "\n";
                }
            }



            //Test if reference and load ctx have the same driver
            PortRef driver_ref = net_ref.second->driver;
            PortRef driver_load = net_load->driver;
            if(driver_ref.cell && driver_ref.cell->name.str(ctx_norm.get()) != "$PACKER_GND"){
                ASSERT_TRUE(driver_load.cell) << "file: " << file.toStdString() << "\n"  << "No net driver found";
                ASSERT_NE(driver_load.cell->bel, BelId());
                ASSERT_EQ(driver_ref.cell->bel.index, driver_load.cell->bel.index)
                        << "file: " << file.toStdString() << "\n"
                        << "\nReference net: " << net_ref.first.str(ctx_norm.get())
                        << "\nLoad net: " << net_load->name.str(ctx_load.get());
            }else{
                ASSERT_FALSE(driver_load.cell) << "file: " << file.toStdString() << "\n"
                                               << "found net driver where none should be";
            }

            //Test if reference and load ctx have the same users
            size_t net_ref_size = net_ref.second->users.size();
            ASSERT_LE(net_ref_size, net_load->users.size())
                    << "file: " << file.toStdString() << "\n"
                    << "reference net " << net_ref.first.str(ctx_norm.get());
            std::unordered_set<CellInfo *> used_cells_load;
            for(auto portRef_ref : net_ref.second->users){
                bool hasUser = false;
                if(!portRef_ref.cell)
                    continue;
                BelId bel = portRef_ref.cell->bel;
                for(auto portRef_load : net_load->users){
                    if(portRef_load.cell && portRef_load.cell->bel.index == bel.index){
                        hasUser = true;
                        used_cells_load.insert(portRef_load.cell);
                        break;
                    }
                }
                ASSERT_TRUE(hasUser)  << "file: " << file.toStdString() << "\n"
                                      << "there is an users in reference context but not in load context";
            }
            if(net_load->users.size() != used_cells_load.size()){
                for(auto portRef_load : net_load->users){
                    if(portRef_load.cell && used_cells_load.find(portRef_load.cell) == used_cells_load.end()){
                        ASSERT_EQ(portRef_load.cell->type, id_ICESTORM_LC) << "file: " << file.toStdString() << "\n" ;
                        ASSERT_TRUE(portRef_load.port ==id_CEN || portRef_load.port == id_CLK || portRef_load.port == id_SR)
                                << "file: " << file.toStdString() << "\n" ;
                    }
                }
            }

        }
    }

    size_t not_reconstructable_cells = 0;
    if(ctx_norm->cells.find(ctx_norm->id("$PACKER_GND")) != ctx_norm->cells.end())
        not_reconstructable_cells++;
    ASSERT_EQ(ctx_norm->cells.size() - not_reconstructable_cells , ctx_load->cells.size())
            << "file: " << file.toStdString() << "\n";

    for(auto &cell_ref : ctx_norm->cells){
        ASSERT_NE(cell_ref.second->bel, BelId()) << "file: " << file.toStdString() << "\n"
                                                 << "found unplaced cell in reference context";
        BelId bel = cell_ref.second->bel;
        if(cell_ref.second->type == id_SB_WARMBOOT || cell_ref.second->type == id_ICESTORM_LFOSC ||
                cell_ref.second->type == id_SB_LEDDA_IP)
            continue;
        if(cell_ref.first.str(ctx_norm.get()) == "$PACKER_GND"){
            continue;
        }
        CellInfo *cell_load = ctx_load->bel_to_cell[bel.index];

        ASSERT_TRUE(cell_load) << "file: " << file.toStdString() << "\n"
                               << "could not find corresponding cell in loaded context";
        ASSERT_EQ(cell_ref.second->type, cell_load->type) << "file: " << file.toStdString() << "\n" ;

        if(cell_ref.second->type == id_ICESTORM_LC){
            const ChipInfoPOD &ci = *ctx_norm->chip_info;
            int lut_init_ref = permute(ctx_norm.get(), get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("LUT_INIT")),
                                            bel, ci);
            EXPECT_EQ(lut_init_ref,
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("LUT_INIT")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("NEG_CLK")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("NEG_CLK")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("DFF_ENABLE")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("DFF_ENABLE")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("ASYNC_SR")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("ASYNC_SR")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("SET_NORESET")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("SET_NORESET")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("CARRY_ENABLE")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("CARRY_ENABLE")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("CIN_SET")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("CIN_SET")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_LC.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
        }else if (cell_ref.second->type == id_SB_IO) {

            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("PIN_TYPE")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("PIN_TYPE")))
                    << "file: " << file.toStdString() << "\n"
                    << id_SB_IO.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("NEG_TRIGGER")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("NEG_TRIGGER")))
                    << "file: " << file.toStdString() << "\n"
                    << id_SB_IO.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("PULLUP")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("PULLUP")))
                    << "file: " << file.toStdString() << "\n"
                    << id_SB_IO.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());

            EXPECT_EQ(cell_ref.second->ioInfo.lvds, cell_load->ioInfo.lvds)
                    << "file: " << file.toStdString() << "\n" ;

            if(!cell_ref.second->ioInfo.lvds && ctx_norm->archArgs().type == ArchArgs::UP5K){
                std::string pullup_resistor_ref;
                if (cell_ref.second->attrs.count(ctx_norm->id("PULLUP_RESISTOR")))
                    pullup_resistor_ref = cell_ref.second->attrs.at(ctx_norm->id("PULLUP_RESISTOR")).as_string();

                std::string pullup_resistor_load;
                if (cell_load->attrs.count(ctx_load->id("PULLUP_RESISTOR")))
                    pullup_resistor_load = cell_load->attrs.at(ctx_load->id("PULLUP_RESISTOR")).as_string();

                EXPECT_EQ(pullup_resistor_ref, pullup_resistor_load)
                        << "file: " << file.toStdString() << "\n" ;
            }
        }else if(cell_ref.second->type == id_SB_GB){

        }else if(cell_ref.second->type == id_ICESTORM_RAM){
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("NEG_CLK_R")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("NEG_CLK_R")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("NEG_CLK_W")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("NEG_CLK_W")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("WRITE_MODE")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("WRITE_MODE")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id("READ_MODE")),
                      get_param_or_def(ctx_load.get(), cell_load, ctx_load->id("READ_MODE")))
                    << "file: " << file.toStdString() << "\n"
                    << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
            for(auto digit : "0123456789ABCDEF"){
                std::string prop_ref = cell_ref.second->params[ctx_norm->id(std::string("INIT_") + digit)].to_string();
                std::string prop_load = cell_load->params[ctx_load->id(std::string("INIT_") + digit)].to_string();
                EXPECT_EQ(prop_ref.size(), prop_load.size())
                        << "file: " << file.toStdString() << "\n"
                        << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
                for(size_t i = 0; i < prop_ref.size(); i++){
                    EXPECT_EQ(prop_ref.at(i) == Property::S1 ? '1' : '0' , prop_load.at(i))
                            << "file: " << file.toStdString() << "\n"
                            << id_ICESTORM_RAM.str(ctx_norm.get()) << " " << cell_ref.first.str(ctx_norm.get());
                }
            }
        }else if(cell_ref.second->type == id_SB_LED_DRV_CUR){

        }else if(cell_ref.second->type == id_SB_RGB_DRV){
            for(auto param : rgb_params_get()){
                EXPECT_EQ(get_param_str_or_def(cell_ref.second.get(), ctx_norm->id(param.first)),
                          get_param_str_or_def(cell_load, ctx_load->id(param.first)))
                        << "file: " << file.toStdString() << "\n"
                        << "cell type: " << id_SB_RGB_DRV.str(ctx_norm.get())
                        << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                        << "\nparameter: "  << param.first;
            }
        }else if(cell_ref.second->type == id_SB_RGBA_DRV){
            for(auto param : rgba_params_get()){
                EXPECT_EQ(get_param_str_or_def(cell_ref.second.get(), ctx_norm->id(param.first)),
                          get_param_str_or_def(cell_load, ctx_load->id(param.first)))
                        << "file: " << file.toStdString() << "\n"
                        << "cell type: " << id_SB_RGBA_DRV.str(ctx_norm.get())
                        << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                        << "\nparameter: "  << param.first;
            }
        }else if(cell_ref.second->type == id_SB_I2C){
            //TODO
        }else if(cell_ref.second->type == id_SB_SPI){

        }else if(cell_ref.second->type == id_ICESTORM_SPRAM) {

        }else if(cell_ref.second->type == id_ICESTORM_DSP){
            for(auto param : mac16_params_get()){
                EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id(param.first)),
                          get_param_or_def(ctx_load.get(), cell_load, ctx_load->id(param.first)))
                        << "file: " << file.toStdString() << "\n"
                        << "cell type: " << id_ICESTORM_DSP.str(ctx_norm.get())
                        << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                        << "\nparameter: "  << param.first;
            }
        }else if(cell_ref.second->type == id_ICESTORM_HFOSC){
            if (ctx_norm->args.type != ArchArgs::U4K){
                for(auto param : hfosc_params_u4k_get()){
                    EXPECT_EQ(get_param_str_or_def(cell_ref.second.get(), ctx_norm->id(param.first)),
                              get_param_str_or_def(cell_load, ctx_load->id(param.first)))
                            << "file: " << file.toStdString() << "\n"
                            << "cell type: " << id_ICESTORM_HFOSC.str(ctx_norm.get())
                            << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                            << "\nparameter: "  << param.first;
                }
            }else{
                for(auto param : hfosc_params_get()){
                    EXPECT_EQ(get_param_str_or_def(cell_ref.second.get(), ctx_norm->id(param.first)),
                              get_param_str_or_def(cell_load, ctx_load->id(param.first)))
                            << "file: " << file.toStdString() << "\n"
                            << "cell type: " << id_ICESTORM_HFOSC.str(ctx_norm.get())
                            << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                            << "\nparameter: "  << param.first;
                }
            }
        }else if(cell_ref.second->type == id_ICESTORM_PLL){
            for(auto param : pll_params_get()){
                EXPECT_EQ(get_param_or_def(ctx_norm.get(), cell_ref.second.get(), ctx_norm->id(param.first)),
                          get_param_or_def(ctx_load.get(), cell_load, ctx_load->id(param.first)))
                        << "file: " << file.toStdString() << "\n"
                        << "cell type: " << id_ICESTORM_PLL.str(ctx_norm.get())
                        << "\nreference cell: " << cell_ref.first.str(ctx_norm.get())
                        << "\nparameter: "  << param.first;
            }
        }
    }
    norm_write_out.clear();
    norm_write_out.seekg(0);
    std::stringstream load_write_out;
    write_asc(ctx_load.get(), load_write_out);

    std::unordered_map<std::string, std::vector<std::string>> map;

    std::string key;
    std::string str_line;
    while(norm_write_out.good()){
        getline(norm_write_out, str_line);
        if(str_line.empty() || str_line.substr(0,4) == ".sym"){
            continue;
        }else if(str_line[0] == '.'){
            key = str_line;
            ASSERT_TRUE(map.find(key) == map.end())
                    << "file: " << file.toStdString() << "\n" ;
            map.insert({key, {}});
        }else if(!key.empty()){
            map.at(key).push_back(str_line);
        }
    }
    size_t line = 0;
    key = "";
    while(load_write_out.good()){
        getline(load_write_out, str_line);
        if(str_line.empty() || str_line.substr(0,4) == ".sym"){
            continue;
        }else if(str_line[0] == '.'){
            if(map.find(key) != map.end()){
                ASSERT_EQ(map.find(key)->second.size(), line)
                        << "file: " << file.toStdString() << "\n" ;
                map.erase(key);
            }
            key = str_line;
            ASSERT_FALSE(map.find(key) == map.end()) << "file: " << file.toStdString() << "\n"
                                                     << "could not find \"" << key << "\" in reference file";
            line = 0;
        }else if(!key.empty()){
            ASSERT_EQ(str_line, map.find(key)->second.at(line))
                    << "file: " << file.toStdString() << "\n" ;
            line++;
        }
    }
    if(map.find(key) != map.end()){
        ASSERT_EQ(map.find(key)->second.size(), line)
                << "file: " << file.toStdString() << "\n" ;
        map.erase(key);
    }

    ASSERT_TRUE(map.empty()) << "file: " << file.toStdString() << "\n"  << map.begin()->first;


}

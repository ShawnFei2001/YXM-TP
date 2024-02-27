#include "Plugin.h"

#include "sqlitecpp/SQLiteCpp.h"


#include "ll/api/Logger.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/schedule/Scheduler.h"
#include "ll/api/schedule/Task.h"
#include "ll/api/service/PlayerInfo.h"

#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/server/commands/CommandBlockName.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandOutputMessageType.h"
#include "mc/server/commands/CommandParameterData.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandRegistry.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/level/Command.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include <ll/api/Config.h>
#include <ll/api/command/DynamicCommand.h>
#include <ll/api/data/KeyValueDB.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerClickEvent.h>
#include <ll/api/event/player/PlayerJoinEvent.h>
#include <ll/api/event/player/PlayerLeaveEvent.h>
#include <ll/api/event/player/PlayerUseItemEvent.h>
#include <ll/api/form/ModalForm.h>
#include <ll/api/form/SimpleForm.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/plugin/NativePlugin.h>
#include <ll/api/service/Bedrock.h>
#include <mc/entity/utilities/ActorType.h>
#include <mc/math/Vec3.h>
#include <mc/network/ServerNetworkHandler.h>
#include <mc/network/packet/PlayerAuthInputPacket.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/item/registry/ItemStack.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>


// Register Particle Scheduler
ll::schedule::GameTickScheduler scheduler;

int currentPlayer = 0;

//
ll::Logger                               dblogger("YXM-TP-DataBase");
static std::unique_ptr<SQLite::Database> db;

//
ll::event::ListenerPtr joinListener;
ll::event::ListenerPtr leaveListener;

// Discover Functions

void UnlockPortal(Player player, std::string portalName) {

}

// Particle Functions

void SpawnParticle(int displayRadius, Vec3 const& pos, std::string const& particleName, int dimId) {
    ll::service::getLevel()->getDimension(dimId)->forEachPlayer([&](Player& player) {
        if (player.getPosition().distanceTo(pos) < displayRadius) {
            SpawnParticleEffectPacket pkt(pos, particleName, dimId, player.getMolangVariables());
            player.sendNetworkPacket(pkt);
        }
        if (player.getPosition().distanceTo(pos) < 1) {
            player.
        }
        return true;
    });
}

// DB Functions

bool initDB() {
    try {
        db = std::make_unique<SQLite::Database>(
            "plugins/YXM-TP/YXM-TP.db",
            SQLite::OPEN_CREATE | SQLite::OPEN_READWRITE
        );
        db->exec("PRAGMA journal_mode = MEMORY");
        db->exec("PRAGMA synchronous = NORMAL");
        db->exec("CREATE TABLE IF NOT EXISTS portal ( \
			PortalName  TEXT PRIMARY KEY \
			UNIQUE \
			NOT NULL, \
            Creator TEXT NOT NULL, \
            DimensionID NUMERIC NOT NULL, \
            x DOUBLE NOT NULL, \
            y DOUBLE NOT NULL, \
            z DOUBLE NOT NULL \
		) \
			WITHOUT ROWID; ");
    } catch (std::exception const& e) {
        dblogger.error("Database Initialization error: {}", e.what());
        return false;
    }
    return true;
}

bool Portal_Init() {
    try {
        SQLite::Statement query(*db, "SELECT PortalName, DimensionID, x, y, z FROM portal");
        while (query.executeStep()) {
            std::string portalName = query.getColumn(0).getText();
            int         dimid      = query.getColumn(1).getInt();
            double      x          = query.getColumn(2).getDouble();
            double      y          = query.getColumn(3).getDouble();
            double      z          = query.getColumn(4).getDouble();
            scheduler.add<ll::schedule::RepeatTask>(std::chrono::milliseconds(1000), [dimid, x, y, z] {
                SpawnParticle(15, Vec3(x, y + 1.3, z), "minecraft:sonic_explosion", dimid);
            });
        }
        return true;
    } catch (const std::exception& e) {
        dblogger.error("Database Portal_Init action error: {}\n", e.what());
        return false;
    }
}

bool Portal_Add(std::string name, std::string creator, int dimid, double x, double y, double z) {
    try {
        SQLite::Statement set{*db, "insert into portal values (?,?,?,?,?,?)"};
        set.bindNoCopy(1, name);
        set.bind(2, creator);
        set.bind(3, dimid);
        set.bind(4, x);
        set.bind(5, y);
        set.bind(6, z);
        set.exec();
        set.reset();
        set.clearBindings();
        return true;
    } catch (std::exception const& e) {
        dblogger.error("Database Portal_Add action error: {}\n", e.what());
        return false;
    }
}

bool Portal_Del(std::string name) {
    try {
        db->exec("DELETE FROM portal WHERE PortalName = '" + name + "';");
        return true;
    } catch (std::exception const& e) {
        dblogger.error("Database Portal_Del action error: {}\n", e.what());
        return false;
    }
}

// Portal Functions

bool Portal_Teleport(Player* player, std::string name) {
    try {
        SQLite::Statement get{*db, "select DimensionID, x, y, z from portal where PortalName = ?"};
        get.bindNoCopy(1, name);
        while (get.executeStep()) {
            int    dimid = (int)get.getColumn(0).getInt();
            double x     = (double)get.getColumn(1).getDouble();
            double y     = (double)get.getColumn(2).getDouble();
            double z     = (double)get.getColumn(3).getDouble();
            player->teleport(Vec3(x, y, z), dimid);
        }
        return true;
    } catch (std::exception const& e) {
        dblogger.error("Database Portal_Teleport error: {}\n", e.what());
        return false;
    }
}


// Portal Command
class PortalCommand : public Command {
    enum PortalOP : int { add = 1, del = 2, tp = 3, re = 4 } op;
    std::string portalName;

public:
    void execute(CommandOrigin const& ori, CommandOutput& outp) const {
        auto*  player  = (Player*)ori.getEntity();
        auto   creator = player->getRealName();
        auto   dimid   = player->getDimensionId();
        Vec3   pos     = player->getFeetPos();
        double x       = pos.x;
        double y       = pos.y;
        double z       = pos.z;
        switch (op) {
        case add:
            if (Portal_Add(portalName, creator, dimid, x, y, z)) {
                player->sendMessage("成功添加传送点: " + portalName);
            } else {
                player->sendMessage("添加传送点失败!该传送点已存在");
            }
            break;
        case del:
            if (Portal_Del(portalName)) {
                player->sendMessage("成功删除传送点: " + portalName);
            } else {
                player->sendMessage("删除传送点失败!请检查该传送点是否存在");
            }
            break;
        case tp:
            if (Portal_Teleport(player, portalName)) {
                player->sendMessage("成功传送至: " + portalName);
            } else {
                player->sendMessage("传送失败!请检查该传送点是否存在");
            }
            break;
        }
        return;
    }

    static void setup(CommandRegistry* registry) {

        // registerCommand
        registry->registerCommand(
            "portal",
            "管理传送点 | Manage portal",
            CommandPermissionLevel::Any,
            {(CommandFlagValue)0},
            {(CommandFlagValue)0x80}
        );
        // addEnum
        registry->addEnum<PortalOP>(
            "add | del | tp",
            {
                {"add", PortalOP::add},
                {"del", PortalOP::del},
                {"tp",  PortalOP::tp }
        }
        );

        // registerOverload
        registry->registerOverload<PortalCommand>(
            "portal",
            CommandParameterData::makeMandatory<CommandParameterDataType::Enum>(
                &PortalCommand::op,
                "添加 | 删除 | 传送",
                "add | del | tp"
            ),
            CommandParameterData::makeMandatory(&PortalCommand::portalName, "传送点名称")
        );
    }
};


namespace plugin {

Plugin::Plugin(ll::plugin::NativePlugin& self) : mSelf(self) {
    auto& logger = mSelf.getLogger();

    // Initialize database.
    if (!initDB()) {
        return;
    }
}

bool Plugin::enable() {
    auto& logger = mSelf.getLogger();

    // Register Commands
    CommandRegistry& reg = ll::service::getCommandRegistry().get();
    PortalCommand::setup(&reg);

    auto& eventBus = ll::event::EventBus::getInstance();

    // Register Listeners
    joinListener =
        eventBus.emplaceListener<ll::event::player::PlayerJoinEvent>([](ll::event::player::PlayerJoinEvent& event) {
            currentPlayer++;
            if (currentPlayer == 1) {
                Portal_Init();
            }
        });

    leaveListener =
        eventBus.emplaceListener<ll::event::player::PlayerLeaveEvent>([&logger](ll::event::player::PlayerLeaveEvent& event) {
            currentPlayer--;
            if (currentPlayer == 0) {
                scheduler.clear();
                logger.info("服务器内现已无玩家,将开启内存优化模式...");
            }
        });


    logger.info(R"(
██╗   ██╗██╗  ██╗███╗   ███╗
╚██╗ ██╔╝╚██╗██╔╝████╗ ████║
 ╚████╔╝  ╚███╔╝ ██╔████╔██║
  ╚██╔╝   ██╔██╗ ██║╚██╔╝██║
   ██║   ██╔╝ ██╗██║ ╚═╝ ██║
   ╚═╝   ╚═╝  ╚═╝╚═╝     ╚═╝
                            )");
    logger.info("YXM-TP Enabled!");
    return true;
}

bool Plugin::disable() {
    auto& logger = mSelf.getLogger();

    // Unregister commands
    auto commandRegistry = ll::service::getCommandRegistry();
    if (!commandRegistry) {
        throw std::runtime_error("failed to get command registry");
    }

    commandRegistry->unregisterCommand("portal");

    // Unregister listeners

    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.removeListener(joinListener);

    eventBus.removeListener(leaveListener);

    logger.info("Disabled");
    return true;
}

} // namespace plugin
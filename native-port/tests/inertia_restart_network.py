"""Exercise native script inertia overrides across SQLite region restarts."""

import argparse
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import tempfile
from xml.etree import ElementTree

from legacy_material_restart_network import OWNER, PRIM, REGION, create_region_fixture
from remote_presence_network import running_server, wait_until


SCRIPT_ASSET = "7b1a5a42-fc36-4936-aa3a-a13535841e70"
SCRIPT_ITEM = "7b1a5a42-fc36-4936-aa3a-a13535841e71"
SCRIPT = b"""
default {
    state_entry() {
        if (llGetObjectDesc() != "") {
            llSetObjectDesc("Unexpected state_entry after restart");
            return;
        }
        llSetBuoyancy(1.0);
        llSetStatus(STATUS_ROTATE_X | STATUS_ROTATE_Z, FALSE);
        osSetInertiaAsSphere(12.0, 1.0, <0.75, 0.0, 0.0>);
        llSetTimerEvent(0.2);
    }

    changed(integer flags) {
        if (flags & CHANGED_REGION_START) {
            llSetBuoyancy(1.0);
            llSetTimerEvent(0.2);
        }
    }

    timer() {
        list inertia = osGetInertiaData();
        float mass = llList2Float(inertia, 0);
        vector local_center = llList2Vector(inertia, 1);
        vector axes = llList2Vector(inertia, 2);
        vector center = llGetCenterOfMass();
        vector root = llGetRootPosition();
        string stage = llGetObjectDesc();
        if (stage == "" || stage == "Override saved") {
            if (llGetStatus(STATUS_ROTATE_X) || !llGetStatus(STATUS_ROTATE_Y) ||
                llGetStatus(STATUS_ROTATE_Z) || llFabs(mass - 12.0) > 0.01 ||
                llVecDist(local_center, <0.75, 0.0, 0.0>) > 0.01 ||
                llVecDist(axes, <0.4, 0.4, 0.4>) > 0.01 ||
                llFabs(center.x - root.x - 0.75) > 0.02)
                return;
            if (stage == "") {
                llSetObjectDesc("Override saved");
                llSetTimerEvent(0.0);
            } else {
                osClearInertia();
                llSetStatus(STATUS_ROTATE_X | STATUS_ROTATE_Z, TRUE);
                llSetObjectDesc("Override restored");
            }
        } else if (stage == "Override restored" || stage == "Override cleared") {
            if (!llGetStatus(STATUS_ROTATE_X) || !llGetStatus(STATUS_ROTATE_Y) ||
                !llGetStatus(STATUS_ROTATE_Z) || mass <= 0.0 || llFabs(mass - 12.0) < 0.01 ||
                llVecMag(local_center) > 0.01 || llVecDist(center, root) > 0.02)
                return;
            if (stage == "Override restored")
                llSetObjectDesc("Override cleared");
            else
                llSetObjectDesc("Automatic inertia restored");
            llSetTimerEvent(0.0);
        }
    }
}
"""


def install_script(region_db, asset_db):
    with closing(sqlite3.connect(asset_db)) as database, database:
        database.execute(
            "INSERT INTO assets (UUID,Name,Description,Type,Local,Temporary,CreatorID,Data) "
            "VALUES (?, 'Inertia restart script', '', 10, 0, 0, ?, ?)", (SCRIPT_ASSET, OWNER, SCRIPT),
        )
    with closing(sqlite3.connect(region_db)) as database, database:
        database.execute(
            "INSERT INTO primitems VALUES (?, ?, ?, ?, 10, 10, 'Inertia restart script', '', 1700000000, "
            "?, ?, ?, '00000000-0000-0000-0000-000000000000', 647168, 647168, 647168, 0, 0, 0)",
            (SCRIPT_ITEM, PRIM, SCRIPT_ASSET, PRIM, OWNER, OWNER, OWNER),
        )


def read_state(region_db):
    with closing(sqlite3.connect(region_db)) as database:
        return database.execute("SELECT Description, PhysInertia, RotationAxisLocks FROM prims WHERE UUID=?", (PRIM,)).fetchone()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--physics-library", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work.resolve()) as directory:
        root = Path(directory)
        region_db = root / "terrain.db"
        asset_db = root / "assets.db"
        config = root / "region.ini"
        create_region_fixture(region_db)
        with closing(sqlite3.connect(region_db)) as database, database:
            database.execute("ALTER TABLE prims ADD COLUMN RotationAxisLocks INTEGER NOT NULL DEFAULT 0")
            database.execute("UPDATE prims SET ObjectFlags=1, DynAttrs='' WHERE UUID=?", (PRIM,))
        config.write_text(
            "[NativeServer]\nEnableLocalRegion=true\nEnableRegionAssets=true\nListenAddress=127.0.0.1\n"
            "UserAccountDatabase=users.db\nAuthenticationDatabase=users.db\n"
            "InventoryDatabase=inventory.db\nGridUserDatabase=gridusers.db\n"
            "[Network]\nhttp_listener_port=0\n"
            "[NativeRegion]\nName=Inertia Restart\nUDPPort=0\nBindAddress=127.0.0.1\n"
            f"ExternalHost=127.0.0.1\nTerrainDatabase=terrain.db\nID={REGION}\n"
            f"Physics=ubODE\nPhysicsLibrary={args.physics_library.resolve()}\n"
            "[Terrain]\nInitialTerrain=flat\n"
            "[Modules]\nAssetServices=LocalAssetServicesConnector\n"
            "[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\n"
            f"StorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:{asset_db};version=3;Pooling=false\n",
            encoding="utf-8",
        )
        subprocess.run([args.native, "--config", str(config), "--init-db"], check=True, timeout=20)
        with running_server(args.native, config, region=True):
            install_script(region_db, asset_db)

        with running_server(args.native, config, region=True):
            wait_until(lambda: read_state(region_db)[0] == "Override saved",
                       "The script did not install and save its inertia override")
        stored = ElementTree.fromstring(read_state(region_db)[1])
        assert stored.tag == "PhysicsInertia"
        assert float(stored.findtext("MASS")) == 12.0
        assert float(stored.findtext("CM/X")) == 0.75
        assert read_state(region_db)[2] == 10

        with running_server(args.native, config, region=True):
            wait_until(lambda: read_state(region_db)[0] == "Override cleared",
                       "The restarted body did not load the override and return to automatic inertia after clearing it")
        assert not read_state(region_db)[1], "Clearing the override must remove it from SQLite"
        assert read_state(region_db)[2] == 0

        with running_server(args.native, config, region=True):
            wait_until(lambda: read_state(region_db)[0] == "Automatic inertia restored",
                       "The third server run did not restore the automatic inertia")
    print("PASS: scripted inertia override and clear survive SQLite region restarts")


if __name__ == "__main__":
    main()

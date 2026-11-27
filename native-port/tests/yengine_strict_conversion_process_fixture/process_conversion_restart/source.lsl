integer gi = xmrString2Integer("0xFFFFFFFF");
float gf = xmrString2Float("1,2,3");
vector gv = xmrString2Vector("<1,2,3>");
rotation gr = xmrString2Rotation("<0,0,0,1>");
integer fi;
float ff;
vector fv;
rotation fr;
integer calls;
integer entries;
integer done;
integer convert() {
    ++calls;
    fi = xmrString2Integer("0xFFFFFFFF");
    ff = xmrString2Float("1,2,3");
    fv = xmrString2Vector("<1,2,3>");
    fr = xmrString2Rotation("<0,0,0,1>");
    return 0;
}
default {
    state_entry() {
        ++entries;
        convert();
        llSetObjectDesc("conversion sleeping");
        llOwnerSay("conversion sleeping");
        llSleep(3.0);
        done = 1;
        llSetObjectDesc("conversion completed");
    }
}
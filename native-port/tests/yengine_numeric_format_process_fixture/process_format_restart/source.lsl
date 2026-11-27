string gi = xmrInteger2String(125,"G2");
string gf = xmrFloat2String(-0.04,"0.000;(0.0)");
string gv = xmrVector2String(<0,0,0>,"#");
string gr = xmrRotation2String(<0,0,0,1>,"F1");
string fi;
string ff;
string fv;
string fr;
integer calls;
integer entries;
integer done;
integer format_values() {
    ++calls;
    fi = xmrInteger2String(125,"G2");
    ff = xmrFloat2String(-0.04,"0.000;(0.0)");
    fv = xmrVector2String(<0,0,0>,"#");
    fr = xmrRotation2String(<0,0,0,1>,"F1");
    return 0;
}
default {
    state_entry() {
        ++entries;
        format_values();
        llSetObjectDesc("format sleeping");
        llOwnerSay("format sleeping");
        llSleep(3.0);
        done = 1;
        llSetObjectDesc("format completed");
    }
}
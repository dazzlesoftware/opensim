rotation value;
integer calls;
integer entries;
integer done;

default {
    state_entry() {
        ++entries;
        ++calls;
        value.x = xmrString2Float("-0");
        value.y = 0.0;
        value.z = xmrString2Float("-0");
        value.s = 0.0;
        llSetObjectDesc("rotation zero sleeping");
        llOwnerSay("rotation zero sleeping");
        llSleep(3.0);
        done = 1;
        llSetObjectDesc("rotation complete:" + (string)((integer)value.s) + ":" +
                        (string)calls + ":" + (string)entries);
    }
}

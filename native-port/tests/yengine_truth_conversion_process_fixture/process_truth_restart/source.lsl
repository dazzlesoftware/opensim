integer gkey = xmrKeyToBool((key)"bad");
integer gstring = xmrStringToBool("00000000-0000-0000-0000-000000000000");
integer glist = xmrListToBool([[]]);
integer gvector = xmrVectorToBool(<0,0,0>);
integer grotation = xmrRotationToBool(<0,0,0,0>);
list result;
integer branch;
integer calls;
integer entries;
integer done;
integer truth_values() {
    ++calls;
    result = [xmrKeyToBool((key)"bad"),
        xmrStringToBool("00000000-0000-0000-0000-000000000000"),
        xmrListToBool([[]]),xmrVectorToBool(<0,0,0>),xmrRotationToBool(<0,0,0,0>)];
    if ((key)"bad") branch = 1;
    return 0;
}
default {
    state_entry() {
        ++entries;
        truth_values();
        llSetObjectDesc("truth sleeping");
        llOwnerSay("truth sleeping");
        llSleep(3.0);
        done = 1;
        llSetObjectDesc("truth completed");
    }
}
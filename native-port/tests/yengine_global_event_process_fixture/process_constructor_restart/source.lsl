integer calls;
integer entries;
list saved;
list result;
string seen;
integer value = initialize();
integer initialize() {
    ++calls;
    llResetScript();
    xmrEventLoadDets([1, "11111111-2222-3333-4444-555555555555", <1,2,3>, 7,
        "22222222-3333-4444-5555-666666666666", "constructor detector", (string)llGetOwner(),
        <4,5,6>, <0,0,0,1>, 3, <7,8,9>,
        <0.125,0.25,0>, <0,0,1>, <0,1,0>, <10,11,12>, <0.5,0.75,0>, 5]);
    saved = xmrEventSaveDets();
    xmrEventEnqueue([21,9]);
    xmrEventLoadDets([1]);
    llSetObjectDesc("initializing:" + (string)calls);
    llOwnerSay("constructor sleeping");
    llSleep(3.0);
    result = xmrEventDequeue(5.0,2097152,0,0,0);
    seen = llDetectedName(0);
    xmrEventLoadDets(saved);
    return 7;
}
default {
    state_entry() {
        ++entries;
        llSetObjectDesc((string)calls + ":" + (string)entries + ":" + (string)value + ":" +
            (string)llList2Integer(result,0) + ":" + (string)llList2Integer(result,1) + ":" +
            seen + ":" + (string)llDetectedLinkNumber(0));
        llOwnerSay("constructor completed");
    }
    touch_start(integer n) {}
}
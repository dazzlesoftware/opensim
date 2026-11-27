integer count;
integer after;
integer entries;
default {
    state_entry() {
        ++entries;
        xmrEventCallHandler([21,7]);
        after = 1;
    }
    touch_start(integer n) {
        count += n;
        llSetObjectDesc("handler sleeping");
        llOwnerSay("handler sleeping");
        llSleep(3.0);
        count += 100;
        llSetObjectDesc((string)count + ":" + (string)after + ":" + (string)entries);
    }
}
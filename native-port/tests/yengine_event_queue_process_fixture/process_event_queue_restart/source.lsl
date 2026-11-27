list shared = [1.234567891, "nested"];
list result;
rotation observed_rotation;
integer entries;
integer complete;
integer returned_w;
default
{
    state_entry()
    {
        entries++;
        xmrEventEnqueue([21, 7, 1.234567891, "payload",
            <.123456789, -.00000049, 1.234567891>,
            (rotation)"<0,0,0,0>", shared, shared]);
        llSetObjectDesc("queue arguments sleeping");
        llSleep(3.0);
        result = xmrEventDequeue(5.0, 2097152, 0, 0, 0);
        observed_rotation = llList2Rot(result, 5);
        returned_w = (integer)observed_rotation.s;
        complete++;
        llSetObjectDesc("queue arguments complete:" + (string)entries + ":" +
            (string)complete + ":" + (string)returned_w + ":" +
            (string)llList2Float(result, 2));
    }
    touch_start(integer n) {}
}

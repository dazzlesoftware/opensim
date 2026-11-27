list result;
list alias;
integer entries;
integer completed;
integer entry_type;
string before;
string after;
integer hold(list argument)
{
    list local = argument;
    before = llList2String(local, 1);
    llSleep(0.1);
    after = llList2String(local, 1);
    completed++;
    return llGetListEntryType(local, 1);
}
default
{
    state_entry()
    {
        entries++;
        result = xmrEventDequeue(0.0, 2097152, 0, 0, 0);
        alias = result;
        entry_type = hold(result);
    }
    touch_start(integer n) {}
}

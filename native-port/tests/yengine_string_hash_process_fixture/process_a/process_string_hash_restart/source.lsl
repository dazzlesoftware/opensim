string input = "restart λ😀";
integer before_hash;
integer result;
integer repeat_hash;
integer entries;
integer complete;
default
{
    state_entry()
    {
        entries++;
        before_hash = xmrHashCode(input);
        llSetObjectDesc("hash sleeping");
        llSleep(3.0);
        result = xmrHashCode(input);
        repeat_hash = xmrHashCode((string)input);
        complete++;
        llSetObjectDesc("hash complete:" + (string)entries + ":" + (string)complete +
            ":" + (string)(result == repeat_hash));
    }
}

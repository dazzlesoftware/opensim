YOption allowall;
integer[] input; object nil;
integer before_wrapper; integer before_backing; integer after_wrapper; integer after_backing;
integer length; integer after;
default { state_entry() {
    input = new integer[](0);
    before_wrapper = ((object)input != nil); before_backing = (input.obj != nil); length = input.Length;
    llSleep(0.1);
    after_wrapper = ((object)input != nil); after_backing = (input.obj != nil); after = 1;
} }

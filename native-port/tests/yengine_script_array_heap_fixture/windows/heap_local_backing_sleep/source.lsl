YOption allowall;
integer[] input; object nil;
integer h0; integer h1; integer h2; integer h3; integer h4; integer left; integer after;
default { state_entry() {
    h0=xmrHeapUsed(); integer[] local=new integer[](4); h1=xmrHeapUsed();
    input=local; object raw=local.obj; h2=xmrHeapUsed();
    llSleep(0.1); h3=xmrHeapUsed(); raw=nil; h4=xmrHeapUsed();
    left=xmrHeapLeft(); after=(local==input);
} }

YOption allowall;
integer[] input; integer[] alias; object nil;
integer h0; integer h1; integer h2; integer h3; integer h4; integer h5;
integer before_backing; integer after_backing; integer after;
default { state_entry() {
    h0=xmrHeapUsed(); input=new integer[](0); h1=xmrHeapUsed(); alias=input; h2=xmrHeapUsed();
    before_backing=(input.obj!=nil); llSleep(0.1); h3=xmrHeapUsed(); after_backing=(input.obj!=nil);
    input.obj=nil; h4=xmrHeapUsed(); input.len0=99; h5=xmrHeapUsed(); after=(input==alias);
} }

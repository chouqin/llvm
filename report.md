% LLVM Complete Integer Project Report
% Qiping Li, Xiaojian Wang, Linchao Zhu


## Objective 

The objective of this project is to 
add systematic support for integers and vectors of integers in all power-of-2
configurations up to N bits in LLVM, where N = 256 (or possibly 512 or 1024).
It involves the following tasks:

* Add first class support for short integer types i1, i2, i4.
* Add first class support for LongInteger types i128, ...
* Add first class support for vectors of integers of i1, i2 and i4 types up to <N x i1>, <N/2 x i2>, <N/4 x i4></N>
* Add first class support for vectors of long integers <N/128 x i128>

## Completion of Task

Up to now, we have done a lot of work in the first 3 tasks
(as specified in the [project requirements], the last task seems the least important).
At the same time, we found some bugs in the LLVM code base,
and tries to fix it to fit our needs.

**Note**: the base LLVM source code is llvm 3.4.

### Investigate LLVM support for short integer types

To test LLVM's support for short integer types(i1, i2, i4), 
for each operation, 
we write some simple llvm IR code to test if operation is supported.
We compile the source code and run it to see if it get correct result.
Here is example of how we testing LLVM's support for `icmp` of two `i1`s.

```
define i32 @main() #0 {
  %c = icmp ult i1 0, 1
  %f = zext i1 %c to i32
  ret i32 %f
}
```

Use `llc` to compile it to assembly code, and use `gcc` and `clang` to compile 
the assembly code to binary executable. Run this executable we can get the result is 0,
which is what we expected.

Using the similar method, we test these operations:

* add, sub
* mul, udiv, sdiv
* shl, lshr, ashr
* and, or, xor
* load, store
* trunc, sext, zext
* bitcast
* icmp with the 10 different comparison types

We found that,
for short integer types,
LLVM don't treat them as first class types.
Instead, for any value of these types, 
LLVM will first promote it into i8 type, which is a first class type in LLVM,
do the operations on i8 type, and cast the result to the original short type.
In all these operations above, `bitcast` has some bug in the LLVM code base,
as documented in the [I2Result](http://parabix.costar.sfu.ca/wiki/I2Result) page.
We fix this bug with in a simple way, 
see the section *Fix Bug of Bitcast* for more details.

### Investigate and legalize operations for i128 and i256

To test LLVM's support for long integers(i128 and i256), 
we use the similar method as above.
We test all these operations for i128 and i256 respectively,
and get this result:

* Almost all operations are supported for i128 and i256
* Multiplication and division is supported on a 64 bit X86 target,
but not on a 32 bit X86 target.
* Multiplication and division is not supported for i256 type.

After investigation, we trying to legalize operations for i128 and i256 type.

### Implement multiplication recursively

For a target on which `UMUL_LOHI` is legal for some integer type, 
we can implement multiplication in a recursive way.
For each `MUL` node which is not legal for its type,
we split this type into its subtype(for example, i126 to i128),
and using the `UMUL_LOHI` of its subtype to implement 
`MUL` of this type. `UMUL_LOHI` of its subtype may
also be illegal, it can be implemented by `UMUL_LOHI` of subtype of its subtype.
So in a recursive way, if `UMUL_LOHI` is legal for some integer type on this target,
we can implement multiplication of any long integer.

We implement a function to decide if a target has `UMUL_LOHI` legal for some integer type.
It return true if can implement UMUL_LOHI for VT recursively
for example, if UMUL_LOHI is supported for i64 on this target,
then i256 can be implemented by path i256 -> i128 -> i64.

See the patch `expand_mul.patch` for more details.

### Try to legalize multiplication for i128 on a 32 bit X86 target

For a 32 bit X86 target, It will call lib function in libgcc(or compiler-rt)
to do the multiplication for i128 type.
For example, multiplication of two i128 integer can call the function `__multi3` in libgcc.
We tried to make this method work but failed.
There two reasons that cause it fails.

First, before doing libcall, 
target will do a lot of house keeping work such as allocate register for arguments and prepare stack,
and return types.
For i128 multiplication, LLVM will split it into four i32 value,
and combine this four value back when finishing libcall.
To store the result of libcall, `X86TargetLowing::LowerCallResult` is called,
which will use register allocation function defined in `X86CallingConv.td`
to allocate register. The register allocation function for 32-bit type integer is defined as:

```
CCIfType<[i32], CCAssignToReg<[EAX, EDX, ECX]>>
```

So it can only allocate 3 32 bit register at the same time.

Another problem is that libgcc or compiler-rt don't define `__multi3` 
if the target is 32 bit. For example, in compiler-rt, 
`__multi3` is defined as:

```c
#if __x86_64
/* Returns: a * b */
ti_int
__multi3(ti_int a, ti_int b)
{
    twords x;
    x.all = a;
    twords y;
    y.all = b;
    twords r;
    r.all = __mulddi3(x.s.low, y.s.low);
    r.s.high += x.s.high * y.s.low + x.s.low * y.s.high;
    return r.all;
}
#endif /* __x86_64 */
```
  
### Add first class support for v64i2

To add first class support for v64i2 type, 
we need the following basic steps:

1. Add entry in `ValueTypes.h` and `Valuetypes.d` to 
make v64i2 a first class type in LLVM. Professor Rob
gave us a script to generate these two files.
We customize it to fit into code base llvm-3.4.

2. Add Register class for v64i2.
  In `X86ISelLowering.cpp`:

  ```
  addRegisterClass(MVT::v64i2, &X86::VR128RegClass);
  ```

  In `X86RegisterInfo.td`, extend VR128 Register class to include v64i2.

3. In `X86CallingConv.td`, extend function to allocate register for v64i2.

4. In `X86IntstrSSE.td`, add entries to auto convert between v64i2 
and all other 128 bit types(v4i32, v2i64...)

5. Determine action for each operation on v64i2. If the action is `Custom`,
  then implement code to do the operation for v64i2 type.
  
  Right now, we have implemented add, sub, mul using bitwise logic.
  Other operations can be implemented in a similar way. See the `v64i2.patch`
  for more details.

## Other Contributions

### Fix Bug of Bitcast

When doing bitcast from vectors of small element size in LLVM IR, we will get wrong results.

For example, `%a = bitcast <2 x i1><i1 0, i1 1> to i2` will set %a to 0 after bitcast.

The cause of this bug is that in LLVM, the above bitcast is accomplished by store and load. 
It first store the vector v2i1 to memory then load from memory as an i2 type. 
But during the `LegalizeVectorOps` phase, it will store each element of the vector separately. 
When the element size is smaller than 8 bits, as in our case i1, 
it will store each element in the **same** location. 
The code that cause this problem is at `LegalizeVectorOps.cpp::ExpandStore`:

```cpp
// Store Stride in bytes
unsigned Stride = ScalarSize/8;
// Extract each of the elements from the original vector
// and save them into memory individually.
SmallVector<SDValue, 8> Stores;
for (unsigned Idx = 0; Idx < NumElem; Idx++) {
  SDValue Ex = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
   RegSclVT, Value, DAG.getConstant(Idx, TLI.getVectorIdxTy()));

  // This scalar TruncStore may be illegal, but we legalize it later.
  SDValue Store = DAG.getTruncStore(Chain, dl, Ex, BasePTR,
   ST->getPointerInfo().getWithOffset(Idx*Stride), MemSclVT,
   isVolatile, isNonTemporal, Alignment, TBAAInfo);

  BasePTR = DAG.getNode(ISD::ADD, dl, BasePTR.getValueType(), BasePTR,

         DAG.getConstant(Stride, BasePTR.getValueType()));

  Stores.push_back(Store);
}
```

If ScalarSize is less than 8, the Stride is 0, 
`ST->getPointerInfo().getWithOffset(Idx*Stride)` will always get the same offset.

To fix this bug, we first convert vector of small element size to an integer 
using *shift and or* before it is saved to the memory. 
Please see the `bitcast.patch` for details.

### Bug when Legalize Vector Loads


We found that `llc` will crash when compile the code below:

```llvm
%at = add i256 1000, 0
%t = bitcast i256 %at to <128 x i2> 
%tt = add <128 x i2> %t, %t
%dd = bitcast <128 x i2> %tt to <8 x i32>
%d = extractelement <8 x i32> %dd, i32 0
```

The cause of this problem is that LLVM uses load and store to
implement `bitcast`. When legalizing vector operations
in `LegalizeVectorOps.cpp`, it will legalize load operations for vectors
in function `VectorLegalizer::ExpandLoad`, As the comment in the code suggests:

```cpp
// When elements in a vector is not byte-addressable, we cannot directly
// load each element by advancing pointer, which could only address bytes.
// Instead, we load all significant words, mask bits off, and concatenate     
// them to form each element. Finally, they are extended to destination 
// scalar type to build the destination vector.
```

But when it computes offset of each element, it gets the offset in bytes(line 470):

```
Offset += LoadBytes;
```


Which should be computed in bits.

And the offset is used in bits(line 498):

```
Offset -= WideBits;
```

This will cause Offset to overflow to a very large unsigned integer.

### Problem of `emitGlobalConstantVector`

When use the following instruction to build a vector of v64i2,

```
%c = bitcast <4 x i32> <i32 1, i32 3, i32 5, i32 7> to <64 x i2>
```

LLVM will first build a vector of v4i32, then bitcast this vector to v64i2.
During the *DAG Combine* phase, it will constant fold this bitcast this bitcast
to a build vector of v62i2, which has 64 constant i2 operands.

Then in the Code Emission Phase, `AsmPrinter` will try to emit 
this constant vector in the data area,
here is how it does that:

```cpp
static void emitGlobalConstantVector(const ConstantVector *CV, AsmPrinter &AP) {
  for (unsigned i = 0, e = CV->getType()->getNumElements(); i != e; ++i)
    emitGlobalConstantImpl(CV->getOperand(i), AP);

  const DataLayout &DL = *AP.TM.getDataLayout();
  unsigned Size = DL.getTypeAllocSize(CV->getType());
  unsigned EmittedSize = DL.getTypeAllocSize(CV->getType()->getElementType()) *
                         CV->getType()->getNumElements();
  if (unsigned Padding = Size - EmittedSize)
    AP.OutStreamer.EmitZeros(Padding);
}
```

It will emit very element of this constant vector.
For each element, it will allocate 1 *byte* because the offset is in bytes.
So this constant vector will get wrong assembly code.
What's worse, in the above code Size is 16 and EmittedSize is 64,
Padding will get a overflow value, which will cause 
the `Asmprinter` emit a lot of zeros to the assembly code.

We fix this by not using constant fold for vector whose element size is less than 8 bits.
Right now, we just add a test to filter v64i2 in `DAGCombiner.cpp`:

```cpp
if (isSimple && N->getValueType(0) != MVT::v64i2)
  return ConstantFoldBITCASTofBUILD_VECTOR(N0.getNode(), DestEltVT);
```

In the future, we can add a more sophisticated condition here.

## Future Works

Due to the limit of time,
there is still much work to be done:

1. We have implemented multiplication of long integers based on `UMUL_LOHI`,
we should also implemented multiplication if `UMUL_LOHI` is not supported on some target.

2. Division for long integers is not implemented yet.

3. Right now we have just implemented a few operations for v64i2 yet,
more operations should be implemented. Some operations(shufflevector)
is very complex, a lot of work needs to be done to do this.

4. We should add first class support for other vectors of short integers(v32i4, v128i1, etc),
This can be implemented in a similar way.

5. Even though not very important, support for vectors of long integers may be added.

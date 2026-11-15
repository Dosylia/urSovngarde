#pragma once

struct AnimationGraphDescriptor
{
    AnimationGraphDescriptor() = default;

    template <std::size_t N, std::size_t O, std::size_t P> AnimationGraphDescriptor(const uint32_t (&acBooleanList)[N], const uint32_t (&acFloatList)[O], const uint32_t (&acIntegerList)[P])
    {
        static_assert(N <= 64, "Too many boolean variables!");
        static_assert((1 + O + P) <= 64, "Too many float and integer!");

        BooleanLookUpTable.assign(acBooleanList, acBooleanList + N);
        FloatLookupTable.assign(acFloatList, acFloatList + O);
        IntegerLookupTable.assign(acIntegerList, acIntegerList + P);
        SharedBooleans = N;
        SharedFloats = O;
        SharedIntegers = P;
    }

    // A variable this game's graph lacks keeps its slot with this index, so the slots after it keep their meaning.
    static constexpr uint32_t kMissing = 0xFFFFFFFF;

    bool IsSynced(uint32_t aIdx) const
    {
        auto itor = std::find(std::begin(BooleanLookUpTable), std::end(BooleanLookUpTable), aIdx);
        if (itor != std::end(BooleanLookUpTable))
            return true;

        itor = std::find(std::begin(FloatLookupTable), std::end(FloatLookupTable), aIdx);
        if (itor != std::end(FloatLookupTable))
            return true;

        itor = std::find(std::begin(IntegerLookupTable), std::end(IntegerLookupTable), aIdx);
        if (itor != std::end(IntegerLookupTable))
            return true;

        return false;
    }

    TiltedPhoques::Vector<uint32_t> BooleanLookUpTable;
    TiltedPhoques::Vector<uint32_t> FloatLookupTable;
    TiltedPhoques::Vector<uint32_t> IntegerLookupTable;

    // Values travel by slot, so two games read a slot alike only if it names the same variable in both.
    // The first slots of each list are the original game graph's, in its order, in every game whose graph comes from it.
    // The slots after them are a behaviour mod's own; a game whose list is not as long as the sender's applies only the first ones.
    uint32_t SharedBooleans = 0;
    uint32_t SharedFloats = 0;
    uint32_t SharedIntegers = 0;
};

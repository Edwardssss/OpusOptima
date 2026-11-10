#include "DemandDrivenChain.hpp"
#include "AssemblySolver.hpp"
#include "LocalOptimizer.hpp"
#include "LifeDeath.hpp"
#include "RowAssembly.hpp"
#include "IO/CandidateArchive.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "TestOutputDirectory.hpp"
#include "Core/LogManager.hpp"
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include "RepoRoot.hpp"

namespace opusoptima::solvers { namespace {
io::PuzzleData ColoredPair(int mask, core::AtomType source) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "static colored pair";
    puzzle.outputMultiplier = 1;
    puzzle.permissionBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 11) | (1ULL << 25);
    if (source != core::AtomType::Fire) puzzle.permissionBits |= (1ULL << 13) | (1ULL << 28);
    if (source != core::AtomType::Fire && source != core::AtomType::Salt)
        puzzle.permissionBits |= 1ULL << 12;
    puzzle.inputs = {{{{source,{0,0}}},{}}};
    puzzle.outputs = {{{{core::AtomType::Fire,{0,0}}, {core::AtomType::Fire,{1,0}}},
                       {{static_cast<std::uint8_t>(mask),{0,0},{1,0}}}}};
    return puzzle;
}

void Transform(io::PuzzleData& puzzle, int rotation) {
    auto position = [&](core::HexCoord cell) {
        for (int i = 0; i < rotation; ++i) cell = core::RotateClockwise(cell);
        return core::Add(cell,{2,-3});
    };
    for (auto& input : puzzle.inputs) {
        for (auto& atom : input.atoms) atom.position = position(atom.position);
        for (auto& bond : input.bonds) {
            bond.from=position(bond.from);bond.to=position(bond.to);
            if(rotation%2) std::swap(bond.from,bond.to);
        }
    }
    for (auto& output : puzzle.outputs) {
        for (auto& atom : output.atoms) atom.position = position(atom.position);
        for (auto& bond : output.bonds) {
            bond.from = position(bond.from);
            bond.to = position(bond.to);
            if (rotation % 2) std::swap(bond.from,bond.to);
        }
        if (rotation % 2) {
            std::reverse(output.atoms.begin(),output.atoms.end());
            std::reverse(output.bonds.begin(),output.bonds.end());
        }
    }
}

void CheckCalcifiedSupply(const io::PuzzleData& puzzle, const io::SolutionData& machine, int expected) {
    int calcifiers=0;
    bool bonderSeen=false;
    for (const auto& object : machine.objects) {
        bonderSeen |= object.typeName=="bonder";
        if (object.typeName!="glyph-calcification") continue;
        ++calcifiers;
        EXPECT_FALSE(bonderSeen);
        const auto input=std::find_if(machine.objects.begin(),machine.objects.end(),[&](const auto& o) {
            if (o.typeName!="input") return false;
            const auto& feed=puzzle.inputs.at(o.bodyData.at(2));
            return core::Add(o.position,feed.atoms.at(0).position)==object.position;
        });
        ASSERT_NE(input,machine.objects.end());
        const auto& feed=puzzle.inputs.at(input->bodyData.at(2));
        ASSERT_EQ(feed.atoms.size(),1u);
        EXPECT_TRUE(feed.bonds.empty());
        const auto type=static_cast<int>(feed.atoms[0].type);
        EXPECT_GE(type,2);
        EXPECT_LE(type,5);
    }
    EXPECT_EQ(calcifiers,expected);
}

#ifdef HAS_LIBVERIFY
void WriteColoredPuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream out(path,std::ios::binary);
    auto word = [&](std::uint64_t value,int bytes) {
        for (int i=0;i<bytes;++i) out.put(static_cast<char>(value>>(8*i)));
    };
    auto cell = [&](core::HexCoord c) { word(c.q,1); word(c.r,1); };
    word(3,4);word(puzzle.name.size(),1);out.write(puzzle.name.data(),puzzle.name.size());
    word(0,8);word(puzzle.permissionBits,8);
    for (const auto* molecules : {&puzzle.inputs,&puzzle.outputs}) {
        word(molecules->size(),4);
        for (const auto& molecule : *molecules) {
            word(molecule.atoms.size(),4);
            for (const auto& atom : molecule.atoms) { word(static_cast<int>(atom.type),1);cell(atom.position); }
            word(molecule.bonds.size(),4);
            for (const auto& bond : molecule.bonds) { word(bond.bits,1);cell(bond.from);cell(bond.to); }
        }
    }
    word(puzzle.outputMultiplier,4);word(0,1);
    out.close();ASSERT_TRUE(out.good());
}
void NativeCheck(const io::PuzzleData& puzzle, io::SolutionData machine, bool accepted = true) {
    test::ScopedTestDirectory scratch;
    const auto input = scratch.Path()/"input.puzzle";
    const auto output = scratch.Path()/"output.solution";
    WriteColoredPuzzle(input,puzzle);
    io::BinaryWriter::WriteSolutionFile(output,machine);
    io::VerificationOptions options;
    options.mode = io::VerificationMode::LegacyGate;
    const auto report = io::VerifySolutionFiles(input,output,options);
    if (!accepted) { EXPECT_FALSE(report.Passed());return; }
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority,io::VerificationAuthority::LegacyGate);
    machine.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output,machine);
    const auto rewritten = io::VerifySolutionFiles(input,output,options);
    ASSERT_TRUE(rewritten.Passed()) << rewritten.detail;
    EXPECT_EQ(rewritten.metrics.cost,machine.metrics.cost);
    EXPECT_EQ(rewritten.metrics.cycles,machine.metrics.cycles);
    EXPECT_EQ(rewritten.metrics.area,machine.metrics.area);
}
#endif

TEST(StaticColoredEmbedding, AllMasksSourcesAndTransforms) {
    for (auto source : {core::AtomType::Fire,core::AtomType::Salt,core::AtomType::Earth})
        for (int mask : {2,4,6,8,10,12,14})
            for (int rotation=0;rotation<6;++rotation) {
                SCOPED_TRACE(static_cast<int>(source));
                SCOPED_TRACE(mask);
                SCOPED_TRACE(rotation);
                auto puzzle=ColoredPair(mask,source);
                Transform(puzzle,rotation);
                const auto candidate=CompileStaticEmbedding(puzzle,
                    std::chrono::steady_clock::now()+std::chrono::seconds(3));
                EXPECT_TRUE(std::any_of(candidate.objects.begin(),candidate.objects.end(),
                    [](const auto& object){return object.typeName=="bonder-prisma";}));
#ifdef HAS_LIBVERIFY
                NativeCheck(puzzle,candidate);
#endif
            }
}

TEST(StaticColoredEmbedding, RefusesUnauthorizedColorsAndPermissions) {
    auto puzzle=ColoredPair(14,core::AtomType::Fire);
    auto denied=puzzle;denied.permissionBits &= ~(1ULL<<11);
    EXPECT_THROW((void)CompileStaticEmbedding(denied),std::invalid_argument);
    for (int mask : {0,3,5,7,9,15,16}) {
        auto invalid=puzzle;invalid.outputs[0].bonds[0].bits=mask;
        EXPECT_THROW((void)CompileStaticEmbedding(invalid),std::invalid_argument) << mask;
    }
    auto wrongType=puzzle;wrongType.outputs[0].atoms[1].type=core::AtomType::Salt;
    EXPECT_THROW((void)CompileStaticEmbedding(wrongType),std::invalid_argument);
    EXPECT_THROW((void)CompileStaticEmbedding(puzzle,std::chrono::steady_clock::now()),std::invalid_argument);
    puzzle.outputs[0].atoms.push_back({core::AtomType::Fire,{0,1}});
    puzzle.outputs[0].atoms.push_back({core::AtomType::Fire,{1,-1}});
    puzzle.outputs[0].bonds.push_back({1,{0,0},{0,1}});
    puzzle.outputs[0].bonds.push_back({1,{0,0},{1,-1}});
    EXPECT_THROW((void)CompileStaticEmbedding(puzzle),std::invalid_argument);
}

TEST(StaticColoredEmbedding, SharedTriangleAndConvertedNeighbor) {
    for (int shape=0;shape<3;++shape) {
        auto enclosed=ColoredPair(14,core::AtomType::Fire);
        enclosed.permissionBits |= 1ULL<<9;
        auto& target=enclosed.outputs[0];
        target.atoms={{core::AtomType::Fire,{0,0}}};
        target.bonds.clear();
        const int spokes=shape==0?3:6;
        std::vector<core::HexCoord> ring;
        core::HexCoord cell{1,0};
        for (int i=0;i<spokes;++i) {
            ring.push_back(cell);
            target.atoms.push_back({core::AtomType::Fire,cell});
            target.bonds.push_back({14,{0,0},cell});
            if (i) target.bonds.push_back({1,ring[i-1],cell});
            cell=core::RotateClockwise(cell);
        }
        if (spokes==6) target.bonds.push_back({1,ring.back(),ring.front()});
        if (shape==2) {
            enclosed.inputs.push_back({{{core::AtomType::Salt,{0,0}}},{}});
            target.atoms.push_back({core::AtomType::Salt,{2,0}});
            target.bonds.push_back({1,{1,0},{2,0}});
        }
        for (int rotation=0;rotation<6;++rotation) {
            SCOPED_TRACE(shape);
            SCOPED_TRACE(rotation);
            auto transformed=enclosed;Transform(transformed,rotation);
            const auto machine=CompileStaticEmbedding(transformed);
            EXPECT_GT(std::count_if(machine.objects.begin(),machine.objects.end(),
                [](const auto& o){return o.typeName=="unbonder";}),0);
#ifdef HAS_LIBVERIFY
            NativeCheck(transformed,machine);
#endif
            transformed.permissionBits &= ~(1ULL<<9);
            EXPECT_THROW((void)CompileStaticEmbedding(transformed),std::invalid_argument);
        }
    }
    auto puzzle=ColoredPair(4,core::AtomType::Fire);
    puzzle.outputs[0].atoms.push_back({core::AtomType::Fire,{0,1}});
    puzzle.outputs[0].bonds.push_back({8,{0,0},{0,1}});
    puzzle.outputs[0].bonds.push_back({2,{0,1},{1,0}});
    for (int rotation=0;rotation<6;++rotation) {
        auto transformed=puzzle;Transform(transformed,rotation);
        const auto machine=CompileStaticEmbedding(transformed);
        EXPECT_EQ(std::count_if(machine.objects.begin(),machine.objects.end(),
            [](const auto& o){return o.typeName=="bonder-prisma";}),1);
#ifdef HAS_LIBVERIFY
        NativeCheck(transformed,machine);
#endif
    }
    puzzle=ColoredPair(14,core::AtomType::Fire);
    puzzle.permissionBits |= 1ULL<<12;
    puzzle.outputs[0].atoms.push_back({core::AtomType::Salt,{0,1}});
    puzzle.outputs[0].bonds.push_back({1,{0,0},{0,1}});
    const auto machine=CompileStaticEmbedding(puzzle);
#ifdef HAS_LIBVERIFY
    NativeCheck(puzzle,machine);
    auto incomplete=CompileStaticEmbedding(ColoredPair(14,core::AtomType::Fire));
    const auto prism=std::find_if(incomplete.objects.begin(),incomplete.objects.end(),
        [](const auto& o){return o.typeName=="bonder-prisma";});
    ASSERT_NE(prism,incomplete.objects.end());
    incomplete.objects.erase(prism);
    NativeCheck(ColoredPair(14,core::AtomType::Fire),incomplete,false);
#endif
}
TEST(StaticDirectEmbedding, TypedBondsAndBothOutputsWithoutChemicalPermissions) {
    auto base=ColoredPair(1,core::AtomType::Fire);
    base.permissionBits=(1ULL<<0)|(1ULL<<8)|(1ULL<<25);
    base.inputs={{{{core::AtomType::Salt,{0,0}}},{}},
                 {{{core::AtomType::Fire,{0,0}}},{}},
                 {{{core::AtomType::Quicksilver,{0,0}}},{}}};
    for(bool twoOutputs : {false,true})
        for(int rotation=0;rotation<6;++rotation) {
            SCOPED_TRACE(twoOutputs);
            SCOPED_TRACE(rotation);
            auto puzzle=base;
            puzzle.outputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Fire,{1,0}},
                               {core::AtomType::Quicksilver,{2,0}}},
                              {{1,{0,0},{1,0}},{1,{1,0},{2,0}}}}};
            if(twoOutputs) {
                puzzle.outputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Fire,{1,0}}},{{1,{0,0},{1,0}}}},
                                {{{core::AtomType::Quicksilver,{0,0}},{core::AtomType::Fire,{1,0}}},{{1,{0,0},{1,0}}}}};
            }
            std::rotate(puzzle.inputs.begin(),puzzle.inputs.begin()+rotation%3,puzzle.inputs.end());
            Transform(puzzle,rotation);
            auto machine=CompileStaticEmbedding(puzzle);
            EXPECT_EQ(std::count_if(machine.objects.begin(),machine.objects.end(),
                [](const auto& o){return o.typeName=="bonder";}),2);
            for(const auto& object:machine.objects) {
                EXPECT_NE(object.typeName,"glyph-calcification");
                EXPECT_NE(object.typeName,"glyph-duplication");
                EXPECT_NE(object.typeName,"bonder-prisma");
                EXPECT_NE(object.typeName,"baron");
                if(object.typeName=="input") {
                    EXPECT_GE(object.bodyData.at(2),0);
                    EXPECT_LT(static_cast<std::size_t>(object.bodyData.at(2)),puzzle.inputs.size());
                }
            }
#ifdef HAS_LIBVERIFY
            NativeCheck(puzzle,machine);
            if(rotation==0) {
                auto missing=machine;
                auto bond=std::find_if(missing.objects.begin(),missing.objects.end(),
                    [](const auto& o){return o.typeName=="bonder";});
                ASSERT_NE(bond,missing.objects.end());
                missing.objects.erase(bond);
                NativeCheck(puzzle,missing,false);
            }
#endif
        }
    base.outputs={base.inputs[0]};
    for(int outputs : {1,2}) {
        if(outputs==2) base.outputs.push_back(base.inputs[1]);
        const auto identity=CompileStaticEmbedding(base);
        EXPECT_EQ(std::count_if(identity.objects.begin(),identity.objects.end(),
            [](const auto& object){return object.typeName=="out-std";}),outputs);
        EXPECT_EQ(std::count_if(identity.objects.begin(),identity.objects.end(),
            [](const auto& object){return object.typeName=="bonder";}),0);
#ifdef HAS_LIBVERIFY
        NativeCheck(base,identity);
        NativeCheck(base,LocalTrial(identity,2));
#endif
    }
}

TEST(StaticDirectEmbedding, UnusedMolecularInputsPreserveActualSingletonIndices) {
    auto base=ColoredPair(1,core::AtomType::Fire);
    base.permissionBits=(1ULL<<0)|(1ULL<<8)|(1ULL<<12)|(1ULL<<25);
    base.inputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Salt,{1,0}}},{{1,{0,0},{1,0}}}},
                 {{{core::AtomType::Air,{2,-1}}},{}}};
    base.outputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Air,{1,0}},
                    {core::AtomType::Salt,{1,1}}},{{1,{0,0},{1,0}},{1,{1,0},{1,1}}}}};
    for(int rotation=0;rotation<6;++rotation)
        for(bool reversed : {false,true}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(reversed);
            auto puzzle=base;
            if(reversed) std::reverse(puzzle.inputs.begin(),puzzle.inputs.end());
            Transform(puzzle,rotation);
            const auto candidate=CompileStaticEmbedding(puzzle);
            int inputs=0,calcifiers=0;
            for(const auto& object:candidate.objects) {
                if(object.typeName=="input") {++inputs;EXPECT_EQ(object.bodyData.at(2),reversed?0:1);}
                if(object.typeName=="glyph-calcification") ++calcifiers;
            }
            EXPECT_EQ(inputs,3);EXPECT_EQ(calcifiers,2);
#ifdef HAS_LIBVERIFY
            NativeCheck(puzzle,candidate);
            NativeCheck(puzzle,LocalTrial(candidate,2));
#endif
        }
    auto forbidden=base;forbidden.permissionBits &= ~(1ULL<<12);
    EXPECT_THROW((void)CompileStaticEmbedding(forbidden),std::invalid_argument);
    base.inputs.pop_back();
    EXPECT_THROW((void)CompileStaticEmbedding(base),std::invalid_argument);
}

TEST(StaticDirectEmbedding, BoundedPurificationUsesGenuineFeedsAndClearTiles) {
    auto p=ColoredPair(1,core::AtomType::Air);
    p.permissionBits=(1ULL<<0)|(1ULL<<8)|(1ULL<<15)|(1ULL<<25);
    p.inputs={{{{core::AtomType::Tin,{0,0}},{core::AtomType::Air,{1,0}}},{{1,{0,0},{1,0}}}},
              {{{core::AtomType::Air,{2,-1}}},{}},
              {{{core::AtomType::Lead,{-3,2}}},{}}};
    p.outputs={{{{core::AtomType::Air,{0,0}},{core::AtomType::Tin,{1,0}},
                  {core::AtomType::Lead,{0,1}},{core::AtomType::Air,{-1,1}}},
                 {{1,{0,0},{1,0}},{1,{0,0},{0,1}},{1,{0,0},{-1,1}}}}};
    for (bool calcified : {false,true}) for (bool reversed : {false,true}) {
        SCOPED_TRACE(calcified);
        auto transformed=p;
        if (calcified) {
            transformed.permissionBits |= 1ULL<<12;
            transformed.outputs[0].atoms[0].type=core::AtomType::Salt;
        }
        if (reversed) {
            std::reverse(transformed.inputs.begin(),transformed.inputs.end());
            transformed.outputs.push_back(transformed.outputs[0]);
        }
        Transform(transformed,reversed?4:1);
        const auto machine=CompileStaticEmbedding(transformed);
        int inputs=0,purifications=0;
        for (const auto& object : machine.objects) {
            if (object.typeName=="glyph-purification") ++purifications;
            if (object.typeName!="input") continue;
            ++inputs;
            const int index=object.bodyData.at(2);
            ASSERT_GE(index,0);
            ASSERT_LT(static_cast<std::size_t>(index),transformed.inputs.size());
            EXPECT_EQ(transformed.inputs[index].atoms.size(),1u);
            EXPECT_TRUE(transformed.inputs[index].bonds.empty());
            EXPECT_NE(index,reversed?2:0);
        }
        EXPECT_EQ(inputs,reversed?10:5);
        EXPECT_EQ(purifications,reversed?2:1);
        CheckCalcifiedSupply(transformed,machine,calcified?(reversed?2:1):0);
#ifdef HAS_LIBVERIFY
        NativeCheck(transformed,machine);
#endif
    }
    auto denied=p;denied.permissionBits &= ~(1ULL<<15);
    EXPECT_THROW((void)CompileStaticEmbedding(denied),std::invalid_argument);
    for (auto metal : {core::AtomType::Iron,core::AtomType::Copper}) {
        auto cascade=p;
        cascade.outputs={{{{core::AtomType::Air,{0,0}},{metal,{1,0}}},{{1,{0,0},{1,0}}}}};
        const auto machine=CompileStaticEmbedding(cascade);
        const int depth=static_cast<int>(core::AtomType::Lead)-static_cast<int>(metal);
        EXPECT_EQ(std::count_if(machine.objects.begin(),machine.objects.end(),
            [](const auto& o) { return o.typeName=="glyph-purification"; }),depth*(depth+1)/2);
        int metalFeeds=0;
        for (const auto& o : machine.objects) if (o.typeName=="input") {
            EXPECT_NE(o.bodyData.at(2),0);
            metalFeeds += o.bodyData.at(2)==2;
        }
        EXPECT_EQ(metalFeeds,depth+1);
#ifdef HAS_LIBVERIFY
        NativeCheck(cascade,machine);
#endif
        auto stable=cascade;stable.permissionBits |= 1ULL<<12;
        stable.outputs[0].atoms[0].type=core::AtomType::Salt;
        const auto stableMachine=CompileStaticEmbedding(stable);
        CheckCalcifiedSupply(stable,stableMachine,1);
#ifdef HAS_LIBVERIFY
        NativeCheck(stable,stableMachine);
#endif
        auto denied=cascade;denied.permissionBits &= ~(1ULL<<15);
        EXPECT_THROW((void)CompileStaticEmbedding(denied),std::invalid_argument);
        EXPECT_THROW((void)CompileStaticEmbedding(cascade,std::chrono::steady_clock::now()),std::invalid_argument);
        if (depth==3) {
            auto nearer=cascade;
            nearer.inputs.push_back({{{core::AtomType::Tin,{2,-2}}},{}});
            nearer.inputs.push_back(nearer.inputs.back());
            const auto selected=CompileStaticEmbedding(nearer);
            int feeds=0;
            for (const auto& o : selected.objects) if (o.typeName=="input" && o.bodyData.at(2)!=1) {
                EXPECT_EQ(o.bodyData.at(2),3);
                ++feeds;
            }
            EXPECT_EQ(feeds,3);
        }
        auto nearest=cascade;
        nearest.inputs.push_back({{{static_cast<core::AtomType>(static_cast<int>(metal)+1),{2,-2}}},{}});
        const auto adjacent=CompileStaticEmbedding(nearest);
        EXPECT_EQ(std::count_if(adjacent.objects.begin(),adjacent.objects.end(),
            [](const auto& o) { return o.typeName=="glyph-purification"; }),1);
    }
    auto missing=p;missing.inputs.pop_back();
    EXPECT_THROW((void)CompileStaticEmbedding(missing),std::invalid_argument);
    auto blocked=p;
    blocked.outputs={{{{core::AtomType::Tin,{0,0}}},{}}};
    core::HexCoord neighbor{1,0};
    for (int rotation=0;rotation<6;++rotation) {
        blocked.outputs[0].atoms.push_back({core::AtomType::Air,neighbor});
        blocked.outputs[0].bonds.push_back({1,{0,0},neighbor});
        neighbor=core::RotateClockwise(neighbor);
    }
    for (auto metal : {core::AtomType::Tin,core::AtomType::Iron}) {
        blocked.outputs[0].atoms[0].type=metal;
        EXPECT_THROW((void)CompileStaticEmbedding(blocked),std::invalid_argument);
    }
    EXPECT_THROW((void)CompileStaticEmbedding(p,std::chrono::steady_clock::now()),std::invalid_argument);
    auto calcified=p;
    calcified.outputs[0].atoms[0].type=core::AtomType::Salt;
    EXPECT_THROW((void)CompileStaticEmbedding(calcified),std::invalid_argument);
    auto wheel=calcified;wheel.permissionBits |= (1ULL<<12)|(1ULL<<13)|(1ULL<<28);
    wheel.outputs[0].atoms[0].type=core::AtomType::Water;
    EXPECT_THROW((void)CompileStaticEmbedding(wheel),std::invalid_argument);
    const auto root=opusoptima::test::RepoRoot(__FILE__);
    const auto enclosed=io::BinaryParser::ParsePuzzleFile(root/"asset/puzzle/campaign/ch4/P034.puzzle");
    // The central metal's only free neighbors are opposite, so no private triangle fits.
    try {
        (void)CompileStaticEmbedding(enclosed);
        FAIL()<<"enclosed purification target unexpectedly accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()),"static embedding Unsupported: bounded purification placement exhausted");
    }
    auto colored=p;colored.permissionBits |= 1ULL<<11;
    colored.inputs.push_back({{{core::AtomType::Fire,{0,0}}},{}});
    colored.outputs[0].atoms[0].type=core::AtomType::Fire;
    colored.outputs[0].atoms[3].type=core::AtomType::Fire;
    colored.outputs[0].bonds[2].bits=14;
    EXPECT_THROW((void)CompileStaticEmbedding(colored),std::invalid_argument);
    auto directPuzzle=p;
    directPuzzle.inputs.push_back({{{core::AtomType::Tin,{1,-2}}},{}});
    const auto direct=CompileStaticEmbedding(directPuzzle);
    EXPECT_FALSE(std::any_of(direct.objects.begin(),direct.objects.end(),
        [](const auto& object) { return object.typeName=="glyph-purification"; }));
#ifdef HAS_LIBVERIFY
    NativeCheck(directPuzzle,direct);
#endif
}

TEST(StaticDirectEmbedding, OneStepProjectionUsesGenuineFeedsAndClearTiles) {
    auto p=ColoredPair(1,core::AtomType::Air);
    p.permissionBits=(1ULL<<0)|(1ULL<<8)|(1ULL<<14)|(1ULL<<25);
    p.inputs={{{{core::AtomType::Tin,{0,0}},{core::AtomType::Quicksilver,{1,0}}},{{1,{0,0},{1,0}}}},
              {{{core::AtomType::Air,{2,-1}}},{}},
              {{{core::AtomType::Lead,{-3,2}}},{}},
              {{{core::AtomType::Quicksilver,{1,-2}}},{}}};
    p.outputs={{{{core::AtomType::Air,{0,0}},{core::AtomType::Quicksilver,{1,0}}},{{1,{0,0},{1,0}}}},
               {{{core::AtomType::Air,{0,0}},{core::AtomType::Tin,{1,0}},{core::AtomType::Lead,{0,1}}},
                 {{1,{0,0},{1,0}},{1,{0,0},{0,1}}}}};
    for (bool calcified : {false,true}) for (bool variant : {false,true}) {
        SCOPED_TRACE(calcified);
        auto transformed=p;
        if (calcified) {
            transformed.permissionBits |= 1ULL<<12;
            transformed.outputs[1].atoms[0].type=core::AtomType::Salt;
        }
        if (variant) {
            std::reverse(transformed.inputs.begin(),transformed.inputs.end());
            std::reverse(transformed.outputs.begin(),transformed.outputs.end());
            Transform(transformed,4);
        }
        const auto machine=CompileStaticEmbedding(transformed);
        int inputs=0,projections=0;
        for (const auto& object : machine.objects) {
            if (object.typeName=="glyph-projection") ++projections;
            if (object.typeName!="input") continue;
            ++inputs;
            const int index=object.bodyData.at(2);
            ASSERT_GE(index,0);
            ASSERT_LT(static_cast<std::size_t>(index),transformed.inputs.size());
            EXPECT_EQ(transformed.inputs[index].atoms.size(),1u);
            EXPECT_TRUE(transformed.inputs[index].bonds.empty());
            EXPECT_NE(index,variant?3:0);
        }
        EXPECT_EQ(inputs,6);
        EXPECT_EQ(projections,1);
        CheckCalcifiedSupply(transformed,machine,calcified?1:0);
#ifdef HAS_LIBVERIFY
        NativeCheck(transformed,machine);
#endif
    }
    auto denied=p;denied.permissionBits &= ~(1ULL<<14);
    EXPECT_THROW((void)CompileStaticEmbedding(denied),std::invalid_argument);
    auto missing=p;missing.inputs.pop_back();
    EXPECT_THROW((void)CompileStaticEmbedding(missing),std::invalid_argument);
    auto multistage=p;multistage.outputs[1].atoms[1].type=core::AtomType::Iron;
    EXPECT_THROW((void)CompileStaticEmbedding(multistage),std::invalid_argument);
    auto missingMetal=p;missingMetal.inputs.erase(missingMetal.inputs.begin()+2);
    EXPECT_THROW((void)CompileStaticEmbedding(missingMetal),std::invalid_argument);
    auto blocked=p;
    blocked.outputs={{{{core::AtomType::Tin,{0,0}}},{}}};
    core::HexCoord neighbor{1,0};
    for (int rotation=0;rotation<6;++rotation) {
        blocked.outputs[0].atoms.push_back({core::AtomType::Air,neighbor});
        blocked.outputs[0].bonds.push_back({1,{0,0},neighbor});
        neighbor=core::RotateClockwise(neighbor);
    }
    EXPECT_THROW((void)CompileStaticEmbedding(blocked),std::invalid_argument);
    EXPECT_THROW((void)CompileStaticEmbedding(p,std::chrono::steady_clock::now()),std::invalid_argument);
    auto calcified=p;
    calcified.outputs[1].atoms[0].type=core::AtomType::Salt;
    EXPECT_THROW((void)CompileStaticEmbedding(calcified),std::invalid_argument);
    auto wheel=calcified;wheel.permissionBits |= (1ULL<<12)|(1ULL<<13)|(1ULL<<28);
    wheel.outputs[1].atoms[0].type=core::AtomType::Water;
    EXPECT_THROW((void)CompileStaticEmbedding(wheel),std::invalid_argument);
    auto colored=p;colored.permissionBits |= 1ULL<<11;
    colored.inputs.push_back({{{core::AtomType::Fire,{0,0}}},{}});
    colored.outputs[0].atoms[0].type=colored.outputs[0].atoms[1].type=core::AtomType::Fire;
    colored.outputs[0].bonds[0].bits=14;
    EXPECT_THROW((void)CompileStaticEmbedding(colored),std::invalid_argument);
    auto direct=p;direct.inputs.push_back({{{core::AtomType::Tin,{1,-2}}},{}});
    const auto directMachine=CompileStaticEmbedding(direct);
    EXPECT_FALSE(std::any_of(directMachine.objects.begin(),directMachine.objects.end(),
        [](const auto& object) { return object.typeName=="glyph-projection"; }));
    auto purified=p;purified.permissionBits |= 1ULL<<15;
    const auto purificationMachine=CompileStaticEmbedding(purified);
    purified.permissionBits &= ~(1ULL<<14);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(purificationMachine),
              io::BinaryWriter::WriteSolution(CompileStaticEmbedding(purified)));
#ifdef HAS_LIBVERIFY
    auto mixed=p;
    mixed.inputs={{{{core::AtomType::Lead,{0,0}}},{}},
                  {{{core::AtomType::Quicksilver,{0,0}}},{}}};
    mixed.permissionBits |= 1ULL<<15;
    mixed.outputs={{{{core::AtomType::Tin,{0,0}},{core::AtomType::Tin,{1,0}}},{{1,{0,0},{1,0}}}}};
    io::SolutionData delayed;delayed.puzzleName=mixed.name;
    auto object=[&](const char* name,core::HexCoord cell,int input=0) {
        io::SolutionObject o;o.typeName=name;o.position=cell;o.bodyData={1,0,input,0,0};
        delayed.objects.push_back(o);
    };
    object("input",{0,0});object("input",{-1,0},1);object("glyph-projection",{-1,0});
    object("input",{1,-1});object("input",{2,-1});object("glyph-purification",{1,-1});
    object("bonder",{0,0});object("out-std",{0,0});object("arm1",{6,6});
    delayed.objects.back().instructions={{'O',96}};
    // Waiting for purification exposes even bonded Tin to the next Mercury dose.
    NativeCheck(mixed,delayed,false);
#endif
}

io::PuzzleData LargeStaticGraph(int count,bool colored) {
    auto p=ColoredPair(1,core::AtomType::Fire);
    p.permissionBits=(1ULL<<0)|(1ULL<<8)|(1ULL<<25);
    if(colored) p.permissionBits |= 1ULL<<11;
    p.inputs={{{{core::AtomType::Salt,{0,0}}},{}},
              {{{core::AtomType::Fire,{0,0}}},{}},
              {{{core::AtomType::Quicksilver,{0,0}}},{}}};
    io::Molecule target;
    target.atoms.push_back({core::AtomType::Salt,{0,0}});
    core::HexCoord near{1,0},far{2,0};
    for(int rotation=0;rotation<6;++rotation) {
        target.atoms.push_back({core::AtomType::Salt,near});
        target.atoms.push_back({core::AtomType::Quicksilver,far});
        near=core::RotateClockwise(near);far=core::RotateClockwise(far);
    }
    for(auto cell : {core::HexCoord{-1,2},core::HexCoord{-2,1},core::HexCoord{-1,-1}})
        if(static_cast<int>(target.atoms.size())<count)
            target.atoms.push_back({core::AtomType::Salt,cell});
    if(colored)
        for(auto& atom:target.atoms)
            if(atom.position==core::HexCoord{1,0} || atom.position==core::HexCoord{2,0})
                atom.type=core::AtomType::Fire;
    for(std::size_t a=0;a<target.atoms.size();++a)
        for(std::size_t b=a+1;b<target.atoms.size();++b) {
            const auto delta=core::Subtract(target.atoms[a].position,target.atoms[b].position);
            if(std::max({std::abs(delta.q),std::abs(delta.r),std::abs(delta.q+delta.r)})!=1) continue;
            const auto mask=colored && target.atoms[a].type==core::AtomType::Fire &&
                target.atoms[b].type==core::AtomType::Fire ? 14 : 1;
            target.bonds.push_back({static_cast<std::uint8_t>(mask),target.atoms[a].position,target.atoms[b].position});
        }
    p.outputs={target};
    return p;
}

TEST(StaticDirectEmbedding, DenseLargerGraphsAndBothOutputPorts) {
    for(int count : {13,14,15,16})
        for(bool colored : {false,true})
            for(int rotation=0;rotation<6;++rotation) {
                SCOPED_TRACE(count);
                SCOPED_TRACE(colored);
                SCOPED_TRACE(rotation);
                auto p=LargeStaticGraph(count,colored);
                ASSERT_GT(p.outputs[0].bonds.size(),12u);
                if(count==16) p.outputs.push_back(LargeStaticGraph(13,!colored).outputs[0]);
                if(count==16) p.permissionBits |= 1ULL<<11;
                std::rotate(p.inputs.begin(),p.inputs.begin()+rotation%3,p.inputs.end());
                Transform(p,rotation);
                const auto machine=CompileStaticEmbedding(p,
                    std::chrono::steady_clock::now()+std::chrono::seconds(3));
#ifdef HAS_LIBVERIFY
                NativeCheck(p,machine);
#endif
            }
    auto tooLarge=LargeStaticGraph(16,false);
    tooLarge.outputs[0].atoms.push_back({core::AtomType::Salt,{1,1}});
    tooLarge.outputs[0].bonds.push_back({1,{1,0},{1,1}});
    EXPECT_THROW((void)CompileStaticEmbedding(tooLarge),std::invalid_argument);
    auto tooWide=LargeStaticGraph(13,false);
    tooWide.outputs[0].atoms.clear();tooWide.outputs[0].bonds.clear();
    for(int q=0;q<8;++q) {
        tooWide.outputs[0].atoms.push_back({core::AtomType::Salt,{q,0}});
        if(q) tooWide.outputs[0].bonds.push_back({1,{q-1,0},{q,0}});
    }
    EXPECT_THROW((void)CompileStaticEmbedding(tooWide),std::invalid_argument);
}

io::PuzzleData ResetSchedulingPuzzle(bool singleColumn) {
    auto p=ColoredPair(1,core::AtomType::Salt);
    p.permissionBits=(1ULL<<0)|(1ULL<<2)|(1ULL<<3)|(1ULL<<8)|(1ULL<<22)|(1ULL<<23)|(1ULL<<25);
    p.outputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Salt,{1,0}},
                  {core::AtomType::Salt,{2,0}},{core::AtomType::Salt,{0,1}},
                  {core::AtomType::Salt,{2,1}}},
                 {{1,{0,0},{1,0}},{1,{1,0},{2,0}},{1,{0,0},{0,1}},{1,{2,0},{2,1}}}}};
    if(singleColumn)
        p.outputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Salt,{0,1}}},{{1,{0,0},{0,1}}}}};
    p.inputs.assign(p.outputs[0].atoms.size(),{{{core::AtomType::Salt,{0,0}}},{}});
    return p;
}

io::PuzzleData SharedMolecularFeedPuzzle() {
    auto puzzle=ColoredPair(1,core::AtomType::Salt);
    puzzle.permissionBits=(1ULL<<32)-1;
    puzzle.inputs={{{{core::AtomType::Salt,{0,0}},{core::AtomType::Salt,{1,0}}},
                    {{1,{0,0},{1,0}}}}};
    puzzle.outputs[0].atoms[0].type=core::AtomType::Vitae;
    puzzle.outputs[0].atoms[1].type=core::AtomType::Mors;
    return puzzle;
}

TEST(SharedMolecularFeed, RejectsUnsupportedDomainsAndExpiredDeadline) {
    auto puzzle=SharedMolecularFeedPuzzle();
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,std::chrono::steady_clock::now()),std::invalid_argument);
    auto rows=ResetSchedulingPuzzle(true);
    std::vector<int> ends;
    EXPECT_THROW((void)CompileRowAssemblyFiniteBatch(rows,AssemblyPlanner::Plan(rows),ends,6,
        std::chrono::steady_clock::now()),std::invalid_argument);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    struct ForwardingCase { int mask; bool postCalcification; int spacing; };
    for(const auto test : {ForwardingCase{2,false,3},ForwardingCase{14,true,5}}) {
        SCOPED_TRACE(test.mask);
        auto proxy=ColoredPair(1,core::AtomType::Fire);
        proxy.permissionBits=(1ULL<<32)-1;
        const auto plan=AssemblyPlanner::Plan(proxy);
        auto labels=proxy.outputs[0];labels.bonds[0].bits=test.mask;
        if(test.postCalcification) labels.atoms[0].type=core::AtomType::Salt;
        const auto old=CompileTriplexRowAssembly(proxy,plan,labels,test.postCalcification,test.spacing);
        const auto bounded=CompileTriplexRowAssembly(proxy,plan,labels,test.postCalcification,
                                                    test.spacing,deadline);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(old),io::BinaryWriter::WriteSolution(bounded));
        if(!test.postCalcification)
            EXPECT_THROW((void)CompileTriplexRowAssembly(proxy,plan,labels,test.postCalcification,
                test.spacing,std::chrono::steady_clock::now()),std::invalid_argument);
    }
    auto colored=puzzle;
    colored.inputs.push_back({{{core::AtomType::Fire,{0,0}}},{}});
    colored.outputs.push_back(ColoredPair(14,core::AtomType::Fire).outputs[0]);
    for(int invalid=0;invalid<3;++invalid) {
        auto rejected=colored;
        if(invalid==0) rejected.permissionBits &= ~(1ULL<<11);
        if(invalid==1) rejected.outputs[1].atoms[0].type=core::AtomType::Salt;
        if(invalid==2) rejected.outputs[1].bonds[0].bits=3;
        EXPECT_THROW((void)CompileLifeDeath(rejected),std::invalid_argument);
        EXPECT_THROW((void)CompileSharedLifeDeath(rejected,deadline),std::invalid_argument);
    }
    EXPECT_THROW((void)CompileSharedLifeDeath(colored,std::chrono::steady_clock::now()),
                 std::invalid_argument);
    puzzle.inputs[0].atoms.resize(1);puzzle.inputs[0].bonds.clear();
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle=SharedMolecularFeedPuzzle();
    puzzle.outputs.resize(3,puzzle.outputs[0]);
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle=SharedMolecularFeedPuzzle();
    puzzle.inputs.insert(puzzle.inputs.begin(),{{{core::AtomType::Salt,{0,0}}},{}});
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle=SharedMolecularFeedPuzzle();
    puzzle.outputs.push_back({{{core::AtomType::Quintessence,{0,0}}},{}});
    EXPECT_NO_THROW((void)CompileSharedLifeDeath(puzzle,deadline));
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,std::chrono::steady_clock::now()),
                 std::invalid_argument);
    puzzle.permissionBits &= ~(1ULL << 18);
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle.inputs.push_back({{{core::AtomType::Quintessence,{0,0}}},{}});
    const auto realQuintessence=CompileSharedLifeDeath(puzzle,deadline);
    EXPECT_FALSE(std::any_of(realQuintessence.objects.begin(),realQuintessence.objects.end(),
        [](const auto& part) { return part.typeName=="glyph-unification"; }));
    puzzle.permissionBits |= 1ULL << 18;
    puzzle.outputs={puzzle.outputs.back()};
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle=SharedMolecularFeedPuzzle();
    puzzle.inputs[0].atoms.push_back({core::AtomType::Lead,{2,0}});
    puzzle.inputs[0].bonds.push_back({1,{1,0},{2,0}});
    puzzle.permissionBits &= ~(1ULL << 12);
    puzzle.outputs.push_back({{{core::AtomType::Quintessence,{0,0}}},{}});
    try {
        (void)CompileSharedLifeDeath(puzzle,deadline);
        FAIL()<<"molecular unification returned a candidate without normalization";
    } catch(const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()),"molecular unification batch unsupported");
    }
    puzzle=SharedMolecularFeedPuzzle();
    puzzle.inputs.push_back({{{core::AtomType::Lead,{0,0}}},{}});
    puzzle.outputs.push_back({{{core::AtomType::Tin,{0,0}}},{}});
    puzzle.permissionBits &= ~(1ULL << 15);
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,deadline),std::invalid_argument);
    puzzle.permissionBits |= 1ULL << 15;
    const auto derived=CompileSharedLifeDeath(puzzle,deadline);
    EXPECT_TRUE(std::any_of(derived.objects.begin(),derived.objects.end(),[](const auto& part) {
        return part.typeName=="glyph-purification";
    }));
    EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,std::chrono::steady_clock::now()),
                 std::invalid_argument);

    puzzle=SharedMolecularFeedPuzzle();
    for(const auto type : {core::AtomType::Vitae,core::AtomType::Mors}) {
        EXPECT_TRUE(StaticEmbeddingMissingLifeFeed(puzzle));
        EXPECT_THROW((void)CompileStaticEmbedding(puzzle,deadline),std::invalid_argument);
        puzzle.inputs.push_back({{{type,{0,0}}},{}});
    }
    EXPECT_FALSE(StaticEmbeddingMissingLifeFeed(puzzle));
    EXPECT_NO_THROW((void)CompileStaticEmbedding(puzzle,deadline));
    puzzle.inputs.back().bonds.push_back({1,{0,0},{1,0}});
    EXPECT_TRUE(StaticEmbeddingMissingLifeFeed(puzzle));
    EXPECT_THROW((void)CompileStaticEmbedding(puzzle,deadline),std::invalid_argument);
    puzzle.outputs.insert(puzzle.outputs.begin(),{{{core::AtomType::Salt,{0,0}}},{}});
    EXPECT_TRUE(StaticEmbeddingMissingLifeFeed(puzzle));
}

#ifdef HAS_LIBVERIFY
TEST(SharedMolecularFeed, NativeSharedProducerConsumesWholeDemand) {
    for(int scenario=0;scenario<10;++scenario) {
        SCOPED_TRACE(scenario);
        const auto root=opusoptima::test::RepoRoot(__FILE__);
        const auto original=root/(scenario==9 ? "asset/puzzle/workshop-versions/w2788067677.puzzle"
                                              : "asset/puzzle/tourney-2021/OM2021_W7.puzzle");
        const bool unchangedOriginal=scenario==7 || scenario==9;
        auto puzzle=unchangedOriginal ? io::BinaryParser::ParsePuzzleFile(original)
                                     : SharedMolecularFeedPuzzle();
        if(scenario==1) Transform(puzzle,2);
        if(scenario==2) {
            puzzle.inputs.push_back({{{core::AtomType::Fire,{0,0}}},{}});
            puzzle.outputs[0].atoms.push_back({core::AtomType::Fire,{2,0}});
            puzzle.outputs[0].bonds.push_back({1,{1,0},{2,0}});
        }
        if(scenario==3) {
            puzzle.inputs.push_back({{{core::AtomType::Fire,{0,0}}},{}});
            puzzle.outputs.insert(puzzle.outputs.begin(),{{{core::AtomType::Fire,{0,0}}},{}});
        }
        if(scenario==4) puzzle.outputs.push_back({{{core::AtomType::Vitae,{0,0}}},{}});
        if(scenario==5) {
            puzzle.inputs.insert(puzzle.inputs.begin(),{{{core::AtomType::Lead,{2,-1}}},{}});
            puzzle.outputs[0].atoms.push_back({core::AtomType::Tin,{2,0}});
            puzzle.outputs[0].bonds.push_back({1,{1,0},{2,0}});
        }
        if(scenario==6) {
            puzzle.inputs={
                {{{core::AtomType::Lead,{2,-1}}},{}},
                {{{core::AtomType::Air,{0,0}},{core::AtomType::Earth,{1,0}}},
                 {{1,{0,0},{1,0}}}},
                {{{core::AtomType::Fire,{0,0}},{core::AtomType::Water,{1,0}}},
                 {{1,{0,0},{1,0}}}}};
            puzzle.outputs.push_back({{{core::AtomType::Quintessence,{0,0}}},{}});
            EXPECT_THROW((void)CompileSharedLifeDeath(puzzle,std::chrono::steady_clock::now()),
                         std::invalid_argument);
        }
        if(scenario==8) {
            puzzle.inputs.insert(puzzle.inputs.begin(),{{{core::AtomType::Lead,{2,-1}}},{}});
            puzzle.inputs.push_back({{{core::AtomType::Fire,{0,0}},{core::AtomType::Fire,{1,0}}},
                                     {{1,{0,0},{1,0}}}});
            puzzle.outputs[0].atoms.insert(puzzle.outputs[0].atoms.end(),
                {{core::AtomType::Fire,{2,0}},{core::AtomType::Fire,{3,0}}});
            puzzle.outputs[0].bonds.insert(puzzle.outputs[0].bonds.end(),
                {{1,{1,0},{2,0}},{14,{2,0},{3,0}}});
        }
        const auto parent=CompileLifeDeath(puzzle);
        const auto child=scenario==9 ? io::SolutionData{} : CompileSharedLifeDeath(puzzle,
            std::chrono::steady_clock::now()+std::chrono::seconds(15));
        test::ScopedTestDirectory scratch;
        const auto input=unchangedOriginal ? original : scratch.Path()/"input.puzzle";
        if(!unchangedOriginal) WriteColoredPuzzle(input,puzzle);
        io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
        options.outerDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
        const auto before=archive.Submit(parent,"test-serial-feed");
        ASSERT_TRUE(before.Passed())<<before.detail;
        if(scenario==9) {
            try {
                (void)CompileSharedLifeDeath(puzzle,
                    std::chrono::steady_clock::now()+std::chrono::seconds(15));
                FAIL()<<"shared feed returned a candidate without a molecular Animismus pair";
            } catch(const std::invalid_argument& error) {
                EXPECT_EQ(std::string(error.what()),"shared feed has no compatible Animismus pair");
            }
            continue;
        }
        if(scenario==5) {
            EXPECT_TRUE(std::any_of(child.objects.begin(),child.objects.end(),[](const auto& part) {
                return part.typeName=="glyph-purification";
            }));
            for(const auto& part : child.objects) {
                if(part.typeName!="input") continue;
                ASSERT_GE(part.bodyData[2],0);
                ASSERT_LT(static_cast<std::size_t>(part.bodyData[2]),puzzle.inputs.size());
                EXPECT_EQ(puzzle.inputs[part.bodyData[2]].atoms[0].type,
                    part.bodyData[2]==0 ? core::AtomType::Lead : core::AtomType::Salt);
            }
        }
        if(scenario==6) {
            std::array<bool,3> used{};
            for(const auto& part : child.objects) {
                if(part.typeName!="input") continue;
                ASSERT_GE(part.bodyData[2],1);
                ASSERT_LT(static_cast<std::size_t>(part.bodyData[2]),puzzle.inputs.size());
                used[part.bodyData[2]]=true;
            }
            EXPECT_TRUE(used[1]);
            EXPECT_TRUE(used[2]);
            EXPECT_TRUE(std::any_of(child.objects.begin(),child.objects.end(),[](const auto& part) {
                return part.typeName=="glyph-unification";
            }));
        }
        if(scenario==8) {
            std::vector<bool> used(puzzle.inputs.size());
            for(const auto& part : child.objects) {
                if(part.typeName!="input") continue;
                ASSERT_GE(part.bodyData[2],0);
                ASSERT_LT(static_cast<std::size_t>(part.bodyData[2]),puzzle.inputs.size());
                used[part.bodyData[2]]=true;
            }
            EXPECT_FALSE(used[0]);
            EXPECT_TRUE(used[1]);
            EXPECT_TRUE(used[2]);
            EXPECT_EQ(puzzle.inputs[1].atoms[0].type,core::AtomType::Salt);
            EXPECT_EQ(puzzle.inputs[2].atoms[0].type,core::AtomType::Fire);
            EXPECT_TRUE(std::any_of(child.objects.begin(),child.objects.end(),[](const auto& part) {
                return part.typeName=="bonder-prisma";
            }));
        }
        const auto after=archive.Submit(child,"test-shared-feed");
        ASSERT_TRUE(after.Passed())<<after.detail;
        EXPECT_EQ(after.authority,io::VerificationAuthority::LegacyGate);
        EXPECT_LT(after.metrics.cost,before.metrics.cost);
        EXPECT_EQ(archive.First()->verification.metrics.cost,before.metrics.cost);
        EXPECT_EQ(std::count_if(child.objects.begin(),child.objects.end(),[](const auto& part) {
            return part.typeName=="out-std";
        }),puzzle.outputs.size());
    }
}
#endif

TEST(RowScheduledLowering, NoOverlapPreservesBytesAndExpiredDeadlineRejects) {
    struct RestoreCompact {
        bool present;std::string value;
        RestoreCompact():present(std::getenv("OPUSOPTIMA_COMPACT_LAYOUT")!=nullptr),
            value(present?std::getenv("OPUSOPTIMA_COMPACT_LAYOUT"):"") {
            setenv("OPUSOPTIMA_COMPACT_LAYOUT","off",1);
        }
        ~RestoreCompact() { if(present) setenv("OPUSOPTIMA_COMPACT_LAYOUT",value.c_str(),1);else unsetenv("OPUSOPTIMA_COMPACT_LAYOUT"); }
    } restoreCompact;
    const auto puzzle=ResetSchedulingPuzzle(true);
    const auto plan=AssemblyPlanner::Plan(puzzle);
    const auto ordinary=CompileRowAssembly(puzzle,plan);
    const auto scheduled=CompileScheduledRowAssembly(puzzle,plan,
        std::chrono::steady_clock::now()+std::chrono::seconds(3));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(scheduled),io::BinaryWriter::WriteSolution(ordinary));
    EXPECT_THROW((void)CompileRetimedRowAssembly(puzzle,plan),std::invalid_argument);
    try {
        (void)CompileScheduledRowAssembly(puzzle,plan,std::chrono::steady_clock::now());
        FAIL()<<"expired construction returned a candidate";
    } catch(const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()),"row assembly deadline");
    }
#ifdef HAS_LIBVERIFY
    NativeCheck(puzzle,scheduled);
#endif
}

TEST(LocalStaticNoopTrial, PreservesEmptyArmTrialAndActiveClockPrograms) {
    const auto original=CompileStaticEmbedding(ColoredPair(14,core::AtomType::Fire));
    auto withEmpty=original;
    auto extra=original.objects.back();extra.instructions.clear();
    extra.position={40,40};++extra.bodyData.at(4);
    withEmpty.objects.push_back(extra);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(withEmpty,2)),io::BinaryWriter::WriteSolution(original));
    auto active=original;
    extra=original.objects.back();extra.instructions={{'R',0},{'r',1}};
    extra.position={40,40};++extra.bodyData.at(4);
    active.objects.push_back(extra);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(active,2)),io::BinaryWriter::WriteSolution(active));
    active=original;
    extra.typeName="baron";extra.instructions={{'R',0}};
    active.objects.push_back(extra);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(active,2)),io::BinaryWriter::WriteSolution(active));
}

TEST(LocalStaticNoopTrial, RemovesOnlyClockRedundantArmsOnExplicitTapes) {
    auto machine=CompileStaticEmbedding(ColoredPair(14,core::AtomType::Fire));
    auto idle=machine.objects.back();
    idle.instructions={{'O',2}};
    auto active=idle;active.position={40,40};++active.bodyData.at(4);
    active.instructions={{'R',0},{'r',3}};
    machine.objects.back()=idle;
    machine.objects.push_back(active);
    auto expected=machine;expected.objects.erase(expected.objects.end()-2);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(machine,2)),io::BinaryWriter::WriteSolution(expected));
    machine.objects[machine.objects.size()-2].instructions={{'O',4}};
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(machine,2)),io::BinaryWriter::WriteSolution(machine));
    machine.objects.back().instructions={{'R',0},{'r',1},{' ',5}};
    EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(machine,2)),io::BinaryWriter::WriteSolution(machine));
    for(char implicit : {'X','C'}) {
        machine.objects[machine.objects.size()-2].instructions={{'O',2}};
        machine.objects.back().instructions={{'R',0},{implicit,3}};
        EXPECT_EQ(io::BinaryWriter::WriteSolution(LocalTrial(machine,2)),io::BinaryWriter::WriteSolution(machine));
    }
}

#ifdef HAS_LIBVERIFY
TEST(LocalStaticNoopTrial, NativeActiveTapePreservesCyclesWithoutRedundantArm) {
    const auto puzzle=ResetSchedulingPuzzle(true);
    auto parent=CompileRowAssembly(puzzle,AssemblyPlanner::Plan(puzzle));
    const auto arm=std::find_if(parent.objects.begin(),parent.objects.end(),[](const auto& object) {
        return object.typeName=="arm1" || object.typeName=="piston";
    });
    ASSERT_NE(arm,parent.objects.end());
    auto extra=*arm;extra.typeName="arm1";extra.position={1000,1000};
    extra.instructions={{'O',0}};extra.bodyData.at(4)=1000;
    parent.objects.push_back(extra);
    test::ScopedTestDirectory scratch;
    const auto input=scratch.Path()/"input.puzzle";WriteColoredPuzzle(input,puzzle);
    io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
    options.outerDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
    const auto before=archive.Submit(parent,"test-active-clock");
    ASSERT_TRUE(before.Passed())<<before.detail;
    const auto child=LocalTrial(parent,2);
    ASSERT_EQ(child.objects.size()+1,parent.objects.size());
    const auto after=archive.Submit(child,"local-v2:explicit-clock");
    ASSERT_TRUE(after.Passed())<<after.detail;
    EXPECT_EQ(after.metrics.cycles,before.metrics.cycles);
    EXPECT_LT(after.metrics.cost,before.metrics.cost);
    EXPECT_LE(after.metrics.area,before.metrics.area);
}

TEST(LocalStaticNoopTrial, NativeCostAreaImprovementRetainsRealFirstAndWheel) {
    for(auto source : {core::AtomType::Fire,core::AtomType::Earth}) {
        SCOPED_TRACE(static_cast<int>(source));
        const auto puzzle=ColoredPair(14,source);
        test::ScopedTestDirectory scratch;
        const auto input=scratch.Path()/"input.puzzle";
        WriteColoredPuzzle(input,puzzle);
        io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
        options.outerDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
        const auto parent=CompileStaticEmbedding(puzzle);
        const auto parentReport=archive.Submit(parent,"portfolio-static-embedding");
        ASSERT_TRUE(parentReport.Passed())<<parentReport.detail;
        const auto firstHash=archive.First()->solutionHash;
        const auto firstBytes=archive.First()->bytes;
        const auto child=LocalTrial(archive.First()->solution,2);
        const auto wheels=[](const auto& machine) {
            return std::count_if(machine.objects.begin(),machine.objects.end(),
                [](const auto& object){return object.typeName=="baron";});
        };
        EXPECT_EQ(wheels(child),wheels(parent));
        const auto childReport=archive.Submit(child,"local-v2:test-static-noop");
        ASSERT_TRUE(childReport.Passed())<<childReport.detail;
        EXPECT_EQ(childReport.authority,io::VerificationAuthority::LegacyGate);
        EXPECT_LT(childReport.metrics.cost,parentReport.metrics.cost);
        EXPECT_LT(childReport.metrics.area,parentReport.metrics.area);
        EXPECT_EQ(childReport.metrics.cycles,parentReport.metrics.cycles);
        EXPECT_EQ(archive.First()->solutionHash,firstHash);
        EXPECT_EQ(archive.First()->bytes,firstBytes);
        ASSERT_TRUE(archive.Slots()[0]);
        ASSERT_TRUE(archive.Slots()[1]);
        ASSERT_TRUE(archive.Slots()[2]);
        EXPECT_EQ(archive.Slots()[0]->verification.metrics.cost,childReport.metrics.cost);
        EXPECT_EQ(archive.Slots()[1]->solutionHash,firstHash);
        EXPECT_EQ(archive.Slots()[2]->verification.metrics.area,childReport.metrics.area);
    }
}

struct StaticSolverLogCapture {
    std::ostringstream output;
    std::shared_ptr<spdlog::logger> logger=GetLoggerOrFallback("solver");
    std::vector<spdlog::sink_ptr> priorSinks=logger->sinks();
    spdlog::level::level_enum priorLevel=logger->level();
    StaticSolverLogCapture() {
        logger->sinks()={std::make_shared<spdlog::sinks::ostream_sink_mt>(output)};
        logger->set_level(spdlog::level::info);
    }
    ~StaticSolverLogCapture() {
        logger->sinks()=priorSinks;
        logger->set_level(priorLevel);
    }
};
void CheckNativeProjectionPortfolio(const char* fixture) {
    struct RestoreClockPortfolio {
        bool presentClock,presentGap;std::string valueClock,valueGap;
        RestoreClockPortfolio():presentClock(std::getenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO")!=nullptr),
            presentGap(std::getenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO")!=nullptr),
            valueClock(presentClock?std::getenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO"):""),
            valueGap(presentGap?std::getenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO"):"") {
            setenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO","off",1);
            setenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO","off",1);
        }
        ~RestoreClockPortfolio() { if(presentClock) setenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO",valueClock.c_str(),1);else unsetenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO"); if(presentGap) setenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO",valueGap.c_str(),1);else unsetenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO"); }
    } restoreClockPortfolio;
    const auto root=opusoptima::test::RepoRoot(__FILE__);
    SCOPED_TRACE(fixture);
    const bool calcified=std::string(fixture)=="ch3/P026";
    const bool cleanup=std::string(fixture)=="journal/journal8/P100";
    const auto input=root/(std::string(cleanup?"asset/puzzle/":"asset/puzzle/campaign/")+fixture+".puzzle");
    const auto puzzle=io::BinaryParser::ParsePuzzleFile(input);
    ASSERT_EQ(puzzle.outputMultiplier,1u);
    ASSERT_EQ(puzzle.outputs.size(),calcified||cleanup?1u:2u);
    ASSERT_TRUE(puzzle.permissionBits & (1ULL<<(cleanup?9:14)));
    ASSERT_FALSE(puzzle.permissionBits & (1ULL<<15));
    test::ScopedTestDirectory scratch;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
    options.outerDeadline=deadline;
    io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
    AssemblySolver solver;solver.SetDeadline(deadline);
    StaticSolverLogCapture capture;
    std::optional<io::VerificationReport> embedding;
    io::SolutionData parent;
    std::string firstHash;
    std::vector<std::byte> firstBytes;
    int submissions=0,accepted=0,embeddingSubmissions=0;
    (void)solver.CollectVerified(puzzle,[&](const auto& candidate,const std::string& stage) {
        ++submissions;
        auto report=archive.Submit(candidate,stage,solver.Deadline());
        if (report.Passed()) {
            ++accepted;
            if (firstHash.empty()) {
                firstHash=archive.First()->solutionHash;
                firstBytes=archive.First()->bytes;
            }
        }
        if (stage=="portfolio-static-embedding") {
            ++embeddingSubmissions;embedding=report;parent=candidate;
        }
        return report;
    },3);
    ASSERT_TRUE(archive.First());
    ASSERT_TRUE(embedding)<<capture.output.str();
    ASSERT_TRUE(embedding->Passed())<<embedding->detail;
    EXPECT_EQ(embedding->authority,io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(embeddingSubmissions,1);
    EXPECT_LE(submissions,12);
    EXPECT_LE(accepted,3);
    EXPECT_EQ(embedding->metrics.cost,cleanup?320:calcified?110:60);
    EXPECT_EQ(embedding->metrics.cycles,3);
    EXPECT_EQ(embedding->metrics.area,cleanup?9:calcified?8:7);
    CheckCalcifiedSupply(puzzle,parent,calcified?3:0);
    const auto child=archive.Submit(LocalTrial(parent,2),"local-v2:test-projection",deadline);
    ASSERT_TRUE(child.Passed())<<child.detail;
    EXPECT_EQ(child.authority,io::VerificationAuthority::LegacyGate);
    EXPECT_LT(child.metrics.cost,embedding->metrics.cost);
    EXPECT_EQ(child.metrics.cycles,embedding->metrics.cycles);
    EXPECT_LT(child.metrics.area,embedding->metrics.area);
    EXPECT_EQ(archive.First()->solutionHash,firstHash);
    EXPECT_EQ(archive.First()->bytes,firstBytes);
    for (const auto& slot : archive.Slots()) {
        ASSERT_TRUE(slot);
        EXPECT_EQ(slot->verification.authority,io::VerificationAuthority::LegacyGate);
        EXPECT_LE(slot->verification.metrics.cycles,embedding->metrics.cycles);
    }
    ::testing::Test::RecordProperty(cleanup?"prism_cleanup_parent":calcified?"calcified_projection_parent":"projection_parent",std::to_string(embedding->metrics.cost)+"/"+
        std::to_string(embedding->metrics.cycles)+"/"+std::to_string(embedding->metrics.area));
    ::testing::Test::RecordProperty(cleanup?"prism_cleanup_child":calcified?"calcified_projection_child":"projection_child",std::to_string(child.metrics.cost)+"/"+
        std::to_string(child.metrics.cycles)+"/"+std::to_string(child.metrics.area));
    const auto text=capture.output.str();
    const auto summary=text.rfind("constructions=");
    ASSERT_NE(summary,std::string::npos);
    EXPECT_LE(std::stoul(text.substr(summary+std::string("constructions=").size())),
              AssemblySolver::MaxPortfolioConstructions);
}

TEST(StaticDirectEmbedding, NativeProjectionPortfolioKeepsOriginalGoalsAndFirst) {
    CheckNativeProjectionPortfolio("ch2/P018");
    CheckNativeProjectionPortfolio("ch3/P026");
    CheckNativeProjectionPortfolio("journal/journal8/P100");
}

TEST(SharedMolecularFeed, NativePortfolioReusesStaticSlotAndRetainsFirst) {
    struct RestoreFlag {
        const char* name;bool present;std::string value;
        explicit RestoreFlag(const char* n):name(n),present(std::getenv(n)!=nullptr),
            value(present?std::getenv(n):"") {}
        ~RestoreFlag() { if(present) setenv(name,value.c_str(),1);else unsetenv(name); }
    } restoreShared("OPUSOPTIMA_SHARED_FEED_RESOURCE"),restoreStatic("OPUSOPTIMA_STATIC_EMBEDDING");
    unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
    std::string originalHash;
    for(int mode=0;mode<4;++mode) {
        SCOPED_TRACE(mode);
        if(mode) setenv("OPUSOPTIMA_SHARED_FEED_RESOURCE","on",1);
        else unsetenv("OPUSOPTIMA_SHARED_FEED_RESOURCE");
        if(mode==3) setenv("OPUSOPTIMA_STATIC_EMBEDDING","off",1);
        else unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
        const auto root=opusoptima::test::RepoRoot(__FILE__);
        const auto fixture=root/"asset/puzzle/tourney-2021/OM2021_W2.puzzle";
        auto puzzle=io::BinaryParser::ParsePuzzleFile(fixture);
        test::ScopedTestDirectory scratch;
        if(mode==2) {
            puzzle.inputs.push_back({{{core::AtomType::Gold,{0,0}}},{}});
            puzzle.outputs.push_back(puzzle.outputs[0]);
        }
        const auto input=mode==2?scratch.Path()/"mixed.puzzle":fixture;
        if(mode==2) WriteColoredPuzzle(input,puzzle);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
        options.outerDeadline=deadline;
        io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
        AssemblySolver solver;solver.SetDeadline(deadline);
        StaticSolverLogCapture capture;
        std::optional<io::VerificationReport> shared;
        (void)solver.CollectVerified(puzzle,[&](const auto& candidate,const std::string& stage) {
            auto report=archive.Submit(candidate,stage,solver.Deadline());
            if(stage=="portfolio-shared-life-death") shared=report;
            return report;
        },3);
        ASSERT_TRUE(archive.First());
        if(mode==0) originalHash=archive.First()->solutionHash;
        if(mode!=2) EXPECT_EQ(archive.First()->solutionHash,originalHash);
        if(mode==1 || mode==2) {
            ASSERT_TRUE(shared)<<capture.output.str();
            EXPECT_TRUE(shared->Passed())<<shared->detail;
            EXPECT_LT(shared->metrics.cost,archive.First()->verification.metrics.cost);
        } else EXPECT_FALSE(shared);
        const auto text=capture.output.str();
        const auto summary=text.rfind("constructions=");
        ASSERT_NE(summary,std::string::npos);
        EXPECT_LE(std::stoul(text.substr(summary+std::string("constructions=").size())),
                  AssemblySolver::MaxPortfolioConstructions);
    }
}

TEST(RowScheduledLowering, NativePortfolioUsesSameSlotAndHonorsOff) {
    struct RestoreFlag {
        const char* name;bool present;std::string value;
        explicit RestoreFlag(const char* n):name(n),present(std::getenv(n)!=nullptr),
            value(present?std::getenv(n):"") {}
        ~RestoreFlag() { if(present) setenv(name,value.c_str(),1);else unsetenv(name); }
    } restoreStatic("OPUSOPTIMA_STATIC_EMBEDDING"),restoreRetiming("OPUSOPTIMA_ROW_RETIMING");
    setenv("OPUSOPTIMA_STATIC_EMBEDDING","off",1);
    for(int mode=0;mode<3;++mode) {
        SCOPED_TRACE(mode);
        if(mode==1) setenv("OPUSOPTIMA_ROW_RETIMING","off",1);
        else unsetenv("OPUSOPTIMA_ROW_RETIMING");
        const auto puzzle=ResetSchedulingPuzzle(mode==2);
        test::ScopedTestDirectory scratch;
        const auto input=scratch.Path()/"input.puzzle";
        WriteColoredPuzzle(input,puzzle);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
        options.outerDeadline=deadline;
        io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
        AssemblySolver solver;solver.SetDeadline(deadline);
        StaticSolverLogCapture capture;
        std::optional<io::VerificationReport> scheduled;
        std::string firstHash;
        (void)solver.CollectVerified(puzzle,[&](const auto& candidate,const std::string& stage) {
            const bool firstPending=!archive.First();
            auto report=archive.Submit(candidate,stage,solver.Deadline());
            if(stage=="portfolio-row-retimed") scheduled=report;
            if(firstPending&&report.Passed()) firstHash=archive.First()->solutionHash;
            return report;
        },3);
        ASSERT_TRUE(archive.First());
        EXPECT_EQ(archive.First()->solutionHash,firstHash);
        if(mode==0) {
            ASSERT_TRUE(scheduled);
            EXPECT_TRUE(scheduled->Passed())<<scheduled->detail;
            EXPECT_EQ(scheduled->authority,io::VerificationAuthority::LegacyGate);
        } else EXPECT_FALSE(scheduled);
        const auto text=capture.output.str();
        const auto summary=text.rfind("constructions=");
        ASSERT_NE(summary,std::string::npos);
        const auto count=std::stoul(text.substr(summary+std::string("constructions=").size()));
        EXPECT_LE(count,AssemblySolver::MaxPortfolioConstructions);
        EXPECT_EQ(text.find("construction=4"),std::string::npos);
    }
}

TEST(StaticColoredEmbedding, NativeFallbackReachesFirstWithoutDuplicatePortfolio) {
    auto puzzle=ColoredPair(14,core::AtomType::Earth);
    puzzle.permissionBits |= (1ULL<<2) | (1ULL<<3) | (1ULL<<22) | (1ULL<<23);
    puzzle.inputs.push_back({{{core::AtomType::Air,{0,0}}},{}});
    puzzle.outputs = {{{{core::AtomType::Fire,{0,0}}, {core::AtomType::Earth,{-2,2}},
        {core::AtomType::Fire,{1,0}}, {core::AtomType::Fire,{-1,1}}, {core::AtomType::Earth,{0,-1}}},
        {{14,{-1,1},{0,0}}, {1,{0,0},{1,0}}, {1,{0,-1},{0,0}}, {1,{-2,2},{-1,1}}}}};
    for (int rotation : {0,3}) {
        auto transformed=puzzle;Transform(transformed,rotation);
        test::ScopedTestDirectory scratch;
        const auto input=scratch.Path()/"input.puzzle";
        WriteColoredPuzzle(input,transformed);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        io::VerificationOptions options;options.mode=io::VerificationMode::LegacyGate;
        options.outerDeadline=deadline;
        io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
        AssemblySolver solver;solver.SetDeadline(deadline);
        int fallback=0,portfolio=0;
        StaticSolverLogCapture capture;
        std::optional<io::VerificationReport> fallbackReport;
        (void)solver.CollectVerified(transformed,[&](const auto& candidate,const std::string& stage) {
            auto report=archive.Submit(candidate,stage,solver.Deadline());
            if(stage=="fallback-static-embedding") {++fallback;fallbackReport=report;}
            if(stage=="portfolio-static-embedding") ++portfolio;
            return report;
        },3);
        ASSERT_TRUE(archive.First());
        EXPECT_EQ(fallback,1);EXPECT_EQ(portfolio,0);
        ASSERT_TRUE(fallbackReport);
        ASSERT_TRUE(fallbackReport->Passed()) << fallbackReport->detail;
        EXPECT_EQ(fallbackReport->authority,io::VerificationAuthority::LegacyGate);
        const auto text=capture.output.str();
        EXPECT_NE(text.find("branch=fallback-static-embedding event=construction count=1 limit=3"),std::string::npos);
        const auto summary=text.rfind("constructions=");
        ASSERT_NE(summary,std::string::npos);
        const auto count=std::stoul(text.substr(summary+std::string("constructions=").size()));
        EXPECT_GE(count,1u);
        EXPECT_LE(count,AssemblySolver::MaxPortfolioConstructions);
        EXPECT_NE(text.find("construction_limit=3",summary),std::string::npos);
    }
}
#endif
} }

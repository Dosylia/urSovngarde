#pragma once

#include <Structs/ReferenceUpdate.h>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <utility>

//! Packs actors' movement into messages that each fit in one network packet, counted in the bits each entry really takes.
//! Movement goes unreliably, and a message bigger than a packet is lost whole when any of its pieces is.
//! An actor whose entry alone passes the limit goes in a message of its own.
//! Used by the client (ClientReferencesMoveRequest), the server's relay and the bot (ServerReferencesMoveRequest).
template <class TMessage> class MovementPacker
{
public:
    //! The most a movement message may take on the wire, the transport's leading byte included.
    static constexpr size_t kMaxBits = 1100 * 8;

    //! aScratch is where sizes are measured; one buffer serves every packer of a frame, and must outlive them.
    MovementPacker(const uint64_t aTick, TiltedPhoques::Buffer& aScratch) noexcept
        : m_pScratch(&aScratch)
    {
        m_message.Tick = aTick;
        m_headerBits = MessageBits();
        m_bits = m_headerBits;
    }

    //! The most bits an actor's entry adds to a message: its server id, then its update.
    //! Computed once and passed to Add, so a relay to several players writes each entry once.
    static size_t EntryBits(TiltedPhoques::Buffer& aScratch, const uint32_t aServerId, const ReferenceUpdate& acUpdate) noexcept
    {
        TiltedPhoques::Buffer::Writer writer(&aScratch);
        TiltedPhoques::Serialization::WriteVarInt(writer, aServerId);
        acUpdate.Serialize(writer);
        // Measured from a byte boundary; in the message it may start mid-byte, and a string in it is written byte-aligned.
        // So it can take up to 7 bits more there, counted here.
        return writer.GetBitPosition() + 7;
    }

    size_t EntryBits(const uint32_t aServerId, const ReferenceUpdate& acUpdate) noexcept { return EntryBits(*m_pScratch, aServerId, acUpdate); }

    //! Adds an actor's update.
    //! When it does not fit beside what is there already, the message so far is moved into aFull, and true is returned: send that first.
    bool Add(const uint32_t aServerId, ReferenceUpdate aUpdate, const size_t aEntryBits, TMessage& aFull) noexcept
    {
        bool full = false;
        if (!m_message.Updates.empty() && m_bits + aEntryBits > kMaxBits)
        {
            const uint64_t cTick = m_message.Tick;
            aFull = std::move(m_message);
            m_message = TMessage{};
            m_message.Tick = cTick;
            m_bits = m_headerBits;
            full = true;
        }
        m_message.Updates[aServerId] = std::move(aUpdate);
        m_bits += aEntryBits;
        return full;
    }

    //! The message being filled: sent last, with nothing after it to wait for.
    TMessage& Current() noexcept { return m_message; }
    bool Empty() const noexcept { return m_message.Updates.empty(); }

    //! The bits the message being filled takes by the sum of its entries.
    size_t CountedBits() const noexcept { return m_bits; }

    //! The bits the message being filled really takes when written, the transport's leading byte included.
    size_t MessageBits() noexcept { return WrittenBits(*m_pScratch, m_message); }

    static size_t WrittenBits(TiltedPhoques::Buffer& aScratch, const TMessage& acMessage) noexcept
    {
        TiltedPhoques::Buffer::Writer writer(&aScratch);
        writer.WriteBits(0, 8);
        acMessage.Serialize(writer);
        return writer.GetBitPosition();
    }

private:
    TiltedPhoques::Buffer* m_pScratch;
    TMessage m_message{};
    size_t m_headerBits{};
    size_t m_bits{};
};

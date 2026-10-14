#include "LicenseManager.h"

namespace
{
    constexpr juce::int64 dayMs = 24LL * 3600 * 1000;

    // HMAC-SHA256 (RFC 2104) on top of juce::SHA256
    juce::MemoryBlock hmacSha256 (const juce::MemoryBlock& key, const juce::MemoryBlock& msg)
    {
        juce::MemoryBlock k (key);
        if (k.getSize() > 64) k = juce::SHA256 (k).getRawData();
        k.setSize (64, true);
        juce::MemoryBlock inner, outer;
        juce::HeapBlock<uint8_t> ipad (64), opad (64);
        for (size_t i = 0; i < 64; ++i) { ipad[i] = (uint8_t) ((uint8_t) k[i] ^ 0x36); opad[i] = (uint8_t) ((uint8_t) k[i] ^ 0x5c); }
        inner.append (ipad.get(), 64); inner.append (msg.getData(), msg.getSize());
        const auto ih = juce::SHA256 (inner).getRawData();
        outer.append (opad.get(), 64); outer.append (ih.getData(), ih.getSize());
        return juce::SHA256 (outer).getRawData();
    }

    // Signing secret, assembled at run time (not a plain string in the binary).
    juce::MemoryBlock secret()
    {
        static const uint8_t mask[] = { 0x5a, 0x13, 0xc7, 0x88, 0x21, 0x9e, 0x44, 0xf0 };
        static const uint8_t enc[] = { 0x0d, 0x5b, 0x86, 0xc4, 0x64, 0xb3, 0x09, 0xb1, 0x09, 0x47, 0x82, 0xda, 0x68, 0xd0, 0x03, 0xdd,
                                       0x6b, 0x23, 0xf7, 0xb9, 0x0c, 0xd7, 0x76, 0xc2, 0x77, 0x43, 0x8e, 0xc6, 0x6a, 0xba, 0x34, 0xa3 };
        juce::MemoryBlock m;
        for (size_t i = 0; i < sizeof (enc); ++i) { const uint8_t b = (uint8_t) (enc[i] ^ mask[i % sizeof (mask)]); m.append (&b, 1); }
        return m;
    }

    juce::String machineId()
    {
        auto id = juce::SystemStats::getUniqueDeviceID();
        if (id.isEmpty()) id = juce::SystemStats::getComputerName() + juce::SystemStats::getLogonName();
        return id;
    }
}

LicenseManager& LicenseManager::get()
{
    static LicenseManager instance = []
    {
        auto main = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        main = main.getChildFile ("Application Support");
       #endif
        main = main.getChildFile ("Whale Audio").getChildFile ("WHALE");
       #if JUCE_WINDOWS
        auto backup = juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("WhaleAudio");
       #elif JUCE_MAC
        auto backup = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Preferences");
       #else
        auto backup = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config");
       #endif
        return LicenseManager (main, backup);
    }();
    return instance;
}

LicenseManager::LicenseManager (juce::File m, juce::File b, Verifier v, Clock c)
    : mainDir (std::move (m)), backupDir (std::move (b)), verifier (std::move (v)), clock (std::move (c)), machine (machineId())
{
    if (! verifier) verifier = &LicenseManager::gumroadVerify;
    load();
}

LicenseManager::~LicenseManager()
{
    pool.removeAllJobs (true, 15000);
}

juce::String LicenseManager::sign (const juce::String& payload) const
{
    const juce::String text = payload + "|" + machine;      // keep the string alive while its UTF-8 bytes are read
    const auto msg = text.toUTF8();
    return juce::String::toHexString (hmacSha256 (secret(), juce::MemoryBlock (msg.getAddress(), msg.sizeInBytes() - 1)).getData(), 32, 0);
}

juce::String LicenseManager::payloadOf (const Record& r)
{
    return "v1|" + juce::String (r.start) + "|" + juce::String (r.seen) + "|" + juce::String (r.activated) + "|" + juce::String (r.checked)
         + "|" + r.key + "|" + r.email;
}

LicenseManager::Record LicenseManager::read (const juce::File& f) const
{
    Record r;
    if (! f.existsAsFile()) return r;
    r.present = true;
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("WHALELICENCE")) return r;
    r.start = xml->getStringAttribute ("start").getLargeIntValue();
    r.seen = xml->getStringAttribute ("seen").getLargeIntValue();
    r.activated = xml->getStringAttribute ("act").getLargeIntValue();
    r.checked = xml->getStringAttribute ("check").getLargeIntValue();
    r.key = xml->getStringAttribute ("key");
    r.email = xml->getStringAttribute ("email");
    r.valid = r.start > 0 && xml->getStringAttribute ("sig") == sign (payloadOf (r));
    return r;
}

void LicenseManager::write (const juce::File& f, const Record& r) const
{
    juce::XmlElement x ("WHALELICENCE");
    x.setAttribute ("start", juce::String (r.start));
    x.setAttribute ("seen", juce::String (r.seen));
    x.setAttribute ("act", juce::String (r.activated));
    x.setAttribute ("check", juce::String (r.checked));
    x.setAttribute ("key", r.key);
    x.setAttribute ("email", r.email);
    x.setAttribute ("sig", sign (payloadOf (r)));
    f.getParentDirectory().createDirectory();
    x.writeTo (f);
}

void LicenseManager::save()
{
    const juce::ScopedLock sl (lock);
    write (mainFile(), rec);
    Record trialOnly;                                     // the backup only remembers the trial
    trialOnly.start = rec.start; trialOnly.seen = rec.seen;
    write (backupFile(), trialOnly);
}

void LicenseManager::load()
{
    if (forced) return;
    const auto now = nowMs();
    const auto m = read (mainFile()), b = read (backupFile());
    {
        const juce::ScopedLock sl (lock);
        rec = {};
        juce::int64 start = 0, seen = 0;
        for (auto* r : { &m, &b })
            if (r->valid) { start = start == 0 ? r->start : std::min (start, r->start); seen = std::max (seen, r->seen); }
        if (start == 0)
            start = (m.present || b.present) ? 1 : now;   // a damaged / copied file ends the trial; nothing at all = first run
        rec.start = start;
        rec.seen = std::max (seen, now);                  // the clock going back does not give days back
        if (m.valid && m.key.isNotEmpty())
        {
            rec.key = m.key; rec.email = m.email; rec.activated = m.activated; rec.checked = m.checked;
            status.store (Status::activated);
        }
        else
        {
            // a licence written on this computer before an OS change (device ID changed): re-activate it in the background
            if (m.present && ! m.valid && m.key.isNotEmpty()) pendingKey = m.key;
            status.store (rec.seen - rec.start < (juce::int64) trialDays * dayMs ? Status::trial : Status::expired);
        }
    }
    save();
}

void LicenseManager::update()
{
    if (forced || status.load() != Status::trial) return;
    const juce::ScopedLock sl (lock);
    if (nowMs() - rec.start >= (juce::int64) trialDays * dayMs) status.store (Status::expired);
}

int LicenseManager::trialDaysLeft() const
{
    const juce::ScopedLock sl (lock);
    if (status.load() != Status::trial) return 0;
    const auto left = rec.start + (juce::int64) trialDays * dayMs - std::max (rec.seen, nowMs());
    return left <= 0 ? 0 : (int) ((left + dayMs - 1) / dayMs);
}

juce::String LicenseManager::getEmail() const { const juce::ScopedLock sl (lock); return rec.email; }
juce::String LicenseManager::getKey() const   { const juce::ScopedLock sl (lock); return rec.key; }

juce::String LicenseManager::normaliseKey (const juce::String& k)
{
    return k.trim().removeCharacters (" \t\r\n").toUpperCase();
}

LicenseManager::Result LicenseManager::activate (const juce::String& rawKey)
{
    auto fr = [] (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); };
    const auto key = normaliseKey (rawKey);
    if (key.length() < 8) return { false, fr ("Paste the key you received by e-mail after your purchase (format XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX).") };
    const auto r = verifier (key, true);
    if (! r.reached)
        return { false, fr ("Cannot reach Gumroad. Check your internet connection and try again (it is only needed once).") };
    if (! r.success)
    {
        if (r.message.containsIgnoreCase ("disabled")) return { false, fr ("This key has been disabled. Write to ") + supportEmail + "." };
        return { false, fr ("Unknown key for WHALE Mastering. Check that it is complete (copy-paste it from the Gumroad e-mail).") };
    }
    if (r.refunded) return { false, fr ("This purchase was refunded: the key is no longer valid.") };
    if (r.uses > maxActivations)
        return { false, fr ("This key is already activated on ") + juce::String (r.uses - 1) + fr (" computers (maximum ") + juce::String (maxActivations)
                        + fr ("). Write to ") + supportEmail + fr (" to free up a computer.") };
    {
        const juce::ScopedLock sl (lock);
        rec.key = key; rec.email = r.email;
        rec.activated = rec.checked = nowMs();
        rec.seen = std::max (rec.seen, rec.activated);
    }
    status.store (Status::activated);
    save();
    return { true, fr ("WHALE Mastering is activated") + (r.email.isNotEmpty() ? fr (" for ") + r.email : juce::String()) + fr (". Thank you!") };
}

void LicenseManager::activateAsync (const juce::String& key, std::function<void (Result)> done)
{
    if (busy.exchange (true)) return;
    pool.addJob ([this, key, done]
    {
        auto res = activate (key);
        busy.store (false);
        juce::MessageManager::callAsync ([res, done] { if (done) done (res); });
    });
}

bool LicenseManager::recheckNow()
{
    juce::String key;
    { const juce::ScopedLock sl (lock); key = rec.key; }
    if (key.isEmpty()) return false;
    const auto r = verifier (key, false);
    if (! r.reached) return true;                        // offline: nothing changes
    const bool revoked = (! r.success && (r.message.containsIgnoreCase ("disabled") || r.message.containsIgnoreCase ("does not exist")))
                      || (r.success && r.refunded);
    {
        const juce::ScopedLock sl (lock);
        rec.checked = nowMs();
        if (revoked) { rec.key = {}; rec.email = {}; rec.activated = 0; }
    }
    if (revoked)
    {
        const juce::ScopedLock sl (lock);
        status.store (rec.seen - rec.start < (juce::int64) trialDays * dayMs ? Status::trial : Status::expired);
    }
    save();
    return ! revoked;
}

void LicenseManager::recheckIfDue()
{
    if (forced) return;
    if (status.load() != Status::activated)
    {
        juce::String k;
        { const juce::ScopedLock sl (lock); k = pendingKey; pendingKey = {}; }
        if (k.isNotEmpty()) activateAsync (k, {});
        return;
    }
    juce::int64 checked;
    { const juce::ScopedLock sl (lock); checked = rec.checked; }
    if (nowMs() - checked < 30 * dayMs || busy.exchange (true)) return;
    pool.addJob ([this] { recheckNow(); busy.store (false); });
}

LicenseManager::VerifyReply LicenseManager::parseReply (const juce::String& body)
{
    VerifyReply r;
    const auto v = juce::JSON::parse (body);
    auto* o = v.getDynamicObject();
    if (o == nullptr || ! o->hasProperty ("success")) return r;
    r.reached = true;
    r.success = (bool) o->getProperty ("success");
    r.message = o->getProperty ("message").toString();
    r.uses = (int) o->getProperty ("uses");
    if (auto* p = o->getProperty ("purchase").getDynamicObject())
    {
        r.email = p->getProperty ("email").toString();
        r.refunded = (bool) p->getProperty ("refunded") || (bool) p->getProperty ("chargebacked") || (bool) p->getProperty ("disputed");
    }
    return r;
}

LicenseManager::VerifyReply LicenseManager::gumroadVerify (const juce::String& key, bool incrementUses)
{
    const auto url = juce::URL ("https://api.gumroad.com/v2/licenses/verify")
                         .withParameter ("product_id", productId)
                         .withParameter ("license_key", key)
                         .withParameter ("increment_uses_count", incrementUses ? "true" : "false");
    juce::WebInputStream s (url, true);                  // POST: the parameters go in the body
    s.withConnectionTimeout (15000).withNumRedirectsToFollow (3);
    s.connect (nullptr);                                 // Gumroad answers 404 + JSON for an unknown key: read the body anyway
    return parseReply (s.readEntireStreamAsString());
}

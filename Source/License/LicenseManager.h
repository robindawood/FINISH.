#pragma once
#include <JuceHeader.h>

// WHALE licence: 7-day full trial, then a Gumroad licence key.
//   - The key is verified online once (Gumroad licence API, no account needed), then WHALE works offline.
//   - The licence file is signed and bound to this computer: copying it elsewhere does not unlock WHALE.
//   - The trial start is kept in two signed files; deleting one does not restart the trial.
//   - Every 30 days an activated copy re-checks the key in the background (refunded / disabled keys are revoked);
//     no connection = nothing happens.
// When the trial is over and WHALE is not activated, the output gets 2 s of silence every 40 s (isLocked()).
class LicenseManager
{
public:
    enum class Status { trial, expired, activated };

    static constexpr int trialDays = 7;
    static constexpr int maxActivations = 5;        // computers per key (reinstalls, a laptop + a studio machine)
    static constexpr const char* productId = "WB5KMMK6rbdoT9Vr-bsfhg==";
    static constexpr const char* supportEmail = "mr.winkbeats@gmail.com";
    static constexpr const char* buyUrl = "";       // Gumroad product page; empty = link hidden until the product is published

    struct VerifyReply
    {
        bool reached = false;       // Gumroad answered (with a JSON body)
        bool success = false;
        int uses = 0;
        bool refunded = false;      // refunded, charged back or disputed
        juce::String email, message;
    };
    using Verifier = std::function<VerifyReply (const juce::String& key, bool incrementUses)>;
    using Clock = std::function<juce::Time()>;

    struct Result { bool ok = false; juce::String message; };

    // Process-wide instance, shared by every WHALE in the session.
    static LicenseManager& get();

    LicenseManager (juce::File mainFolder, juce::File backupFolder, Verifier verifier = {}, Clock clock = {});
    ~LicenseManager();

    void load();                                     // reads / creates the files, sets the status
    Status getStatus() const noexcept               { return status.load(); }
    bool isLocked() const noexcept                  { return status.load() == Status::expired; }
    void update();                                   // the trial can end while WHALE is open (call now and then)
    int trialDaysLeft() const;                       // 1..7 during the trial, 0 after
    juce::String getEmail() const;
    juce::String getKey() const;

    // Blocking (network): call from a background thread, or use activateAsync.
    Result activate (const juce::String& key);
    // Runs activate() off the message thread; done is called on the message thread.
    void activateAsync (const juce::String& key, std::function<void (Result)> done);
    // Background re-check of an activated key, at most every 30 days (call at startup).
    void recheckIfDue();
    bool recheckNow();                               // blocking; true if the licence is still valid

    static VerifyReply gumroadVerify (const juce::String& key, bool incrementUses);
    static VerifyReply parseReply (const juce::String& body);
    static juce::String normaliseKey (const juce::String&);

    // Tests / offline renders: force a status without touching the files.
    void forceStatus (Status s) noexcept            { forced = true; status.store (s); }

    juce::File mainFile() const                     { return mainDir.getChildFile ("licence.whale"); }
    juce::File backupFile() const                   { return backupDir.getChildFile ("com.whaleaudio.whale.dat"); }

private:
    struct Record { bool present = false, valid = false; juce::int64 start = 0, seen = 0, activated = 0, checked = 0; juce::String key, email; };
    Record read (const juce::File&) const;
    void write (const juce::File&, const Record&) const;
    void save();
    juce::String sign (const juce::String& payload) const;
    static juce::String payloadOf (const Record&);
    juce::int64 nowMs() const { return clock ? clock().toMilliseconds() : juce::Time::currentTimeMillis(); }

    juce::File mainDir, backupDir;
    Verifier verifier;
    Clock clock;
    juce::String machine;
    mutable juce::CriticalSection lock;
    Record rec;                                      // current state (guarded by lock)
    juce::String pendingKey;
    std::atomic<Status> status { Status::trial };
    std::atomic<bool> busy { false };
    bool forced = false;
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("WHALE licence").withNumberOfThreads (1) };
};

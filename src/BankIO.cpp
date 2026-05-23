#include "BankIO.h"

namespace genvst::bank
{
namespace
{
    juce::var rowToVar (const BankRow& r)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("type",      r.type);
        obj->setProperty ("slot",      r.slot);
        obj->setProperty ("patchPath", r.patchPath);

        auto* routing = new juce::DynamicObject();
        routing->setProperty ("midiCh",       r.midiCh);
        routing->setProperty ("transposeSt",  r.transposeSt);
        routing->setProperty ("transposeOct", r.transposeOct);
        routing->setProperty ("noteLo",       r.noteLo);
        routing->setProperty ("noteHi",       r.noteHi);
        routing->setProperty ("detuneCents",  r.detuneCents);
        routing->setProperty ("balance",      (double) r.balance);
        obj->setProperty ("routing", juce::var (routing));

        return juce::var (obj);
    }

    // Pull `key` from `obj` if present and an int, else use `fallback`.
    int readInt (const juce::DynamicObject& obj, const char* key, int fallback)
    {
        if (! obj.hasProperty (key)) return fallback;
        const auto v = obj.getProperty (key);
        if (v.isInt() || v.isInt64() || v.isDouble())
            return (int) v;
        return fallback;
    }

    float readFloat (const juce::DynamicObject& obj, const char* key, float fallback)
    {
        if (! obj.hasProperty (key)) return fallback;
        const auto v = obj.getProperty (key);
        if (v.isDouble() || v.isInt() || v.isInt64())
            return (float) (double) v;
        return fallback;
    }

    juce::String readString (const juce::DynamicObject& obj, const char* key)
    {
        if (! obj.hasProperty (key)) return {};
        const auto v = obj.getProperty (key);
        return v.isString() ? v.toString() : juce::String();
    }

    BankRow varToRow (const juce::var& v)
    {
        BankRow r;
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return r;

        r.type      = readString (*obj, "type");
        r.slot      = readInt   (*obj, "slot", 0);
        r.patchPath = readString (*obj, "patchPath");

        if (obj->hasProperty ("routing"))
        {
            const auto routingVar = obj->getProperty ("routing");
            if (auto* routing = routingVar.getDynamicObject())
            {
                r.midiCh       = readInt   (*routing, "midiCh",       0);
                r.transposeSt  = readInt   (*routing, "transposeSt",  0);
                r.transposeOct = readInt   (*routing, "transposeOct", 0);
                r.noteLo       = readInt   (*routing, "noteLo",       0);
                r.noteHi       = readInt   (*routing, "noteHi",       127);
                r.detuneCents  = readInt   (*routing, "detuneCents",  0);
                r.balance      = readFloat (*routing, "balance",      0.0f);
            }
        }
        return r;
    }
}

juce::String toJson (const Bank& bank)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", bank.version);

    juce::Array<juce::var> rows;
    rows.ensureStorageAllocated ((int) bank.rows.size());
    for (const auto& r : bank.rows)
        rows.add (rowToVar (r));
    root->setProperty ("rows", juce::var (rows));

    return juce::JSON::toString (juce::var (root), true /* pretty */);
}

Bank fromJson (const juce::String& json, juce::String& error)
{
    error.clear();

    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed())
    {
        error = "Bank file is not valid JSON: " + result.getErrorMessage();
        return {};
    }

    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        error = "Bank file root is not an object.";
        return {};
    }

    Bank b;
    b.version = readInt (*root, "version", 0);

    if (b.version != kCurrentVersion)
    {
        error = "Unsupported bank file version: " + juce::String (b.version)
              + " (expected " + juce::String (kCurrentVersion) + ").";
        return {};
    }

    if (root->hasProperty ("rows"))
    {
        const auto rowsVar = root->getProperty ("rows");
        if (auto* arr = rowsVar.getArray())
        {
            b.rows.reserve ((std::size_t) arr->size());
            for (const auto& v : *arr)
                b.rows.push_back (varToRow (v));
        }
    }
    return b;
}

juce::String writeToFile (const Bank& bank, const juce::File& file)
{
    const auto json = toJson (bank);
    if (file.exists() && ! file.hasWriteAccess())
        return "Bank file is not writable: " + file.getFullPathName();
    if (! file.replaceWithText (json))
        return "Could not write bank file: " + file.getFullPathName();
    return {};
}

juce::String readFromFile (const juce::File& file, Bank& outBank)
{
    if (! file.existsAsFile())
        return "Bank file does not exist: " + file.getFullPathName();

    const auto json = file.loadFileAsString();
    if (json.isEmpty())
        return "Bank file is empty: " + file.getFullPathName();

    juce::String err;
    outBank = fromJson (json, err);
    return err;
}

} // namespace genvst::bank

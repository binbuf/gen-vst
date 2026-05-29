#include "PluginState.h"

namespace genvst::state
{
    namespace
    {
        constexpr const char* kPatchTag        = "patch";
        constexpr const char* kPatchModeAttr   = "mode";
        constexpr const char* kPatchPathAttr   = "path";
        constexpr const char* kCustomRootsTag  = "customRoots";
        constexpr const char* kRootElementTag  = "root";
        constexpr const char* kKitTag          = "kit";

        void appendPatchPathIfPresent (juce::XmlElement&   parent,
                                       const char*         modeLabel,
                                       const juce::String& path)
        {
            if (path.isEmpty()) return;
            auto* p = parent.createNewChildElement (kPatchTag);
            p->setAttribute (kPatchModeAttr, modeLabel);
            p->setAttribute (kPatchPathAttr, path);
        }
    }

    std::unique_ptr<juce::XmlElement> save (
        const juce::ValueTree&             apvtsState,
        const juce::String&                activeFmPath,
        const juce::String&                activeSqPath,
        const std::vector<juce::String>&   customRootPaths,
        const juce::String&                kitJson)
    {
        auto root = std::make_unique<juce::XmlElement> (kRootTag);

        appendPatchPathIfPresent (*root, "FM", activeFmPath);
        appendPatchPathIfPresent (*root, "SQ", activeSqPath);

        // Embedded drum kit (ADR-0021 amendment). Stored as the `.gnkit` JSON
        // text so the project carries the full kit independently of the
        // on-disk source files.
        if (kitJson.isNotEmpty())
            root->createNewChildElement (kKitTag)->addTextElement (kitJson);

        auto* customRoots = root->createNewChildElement (kCustomRootsTag);
        for (const auto& path : customRootPaths)
        {
            if (path.isEmpty()) continue;
            auto* rootEl = customRoots->createNewChildElement (kRootElementTag);
            rootEl->setAttribute (kPatchPathAttr, path);
        }

        if (auto apvtsXml = apvtsState.createXml())
            root->addChildElement (apvtsXml.release());

        return root;
    }

    std::optional<PendingRestore> restore (
        juce::AudioProcessorValueTreeState& apvts,
        const juce::XmlElement&             xml)
    {
        if (! xml.hasTagName (kRootTag))
            return std::nullopt;

        PendingRestore pending;
        const auto apvtsRootTag = apvts.state.getType();

        for (auto* child : xml.getChildIterator())
        {
            if (child->hasTagName (kPatchTag))
            {
                const auto mode = child->getStringAttribute (kPatchModeAttr);
                const auto path = child->getStringAttribute (kPatchPathAttr);
                if (path.isEmpty()) continue;
                if      (mode == "FM") pending.activeFmPath = path;
                else if (mode == "SQ") pending.activeSqPath = path;
            }
            else if (child->hasTagName (kCustomRootsTag))
            {
                for (auto* rootEl : child->getChildIterator())
                {
                    if (! rootEl->hasTagName (kRootElementTag)) continue;
                    const auto path = rootEl->getStringAttribute (kPatchPathAttr);
                    if (path.isNotEmpty()) pending.customRoots.push_back (path);
                }
            }
            else if (child->hasTagName (kKitTag))
            {
                pending.kitJson = child->getAllSubText();
            }
            else if (child->hasTagName (apvtsRootTag))
            {
                apvts.replaceState (juce::ValueTree::fromXml (*child));
            }
        }

        return pending;
    }
}

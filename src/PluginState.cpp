#include "PluginState.h"

#include "PluginProcessor.h"

namespace genvst::state
{
    std::unique_ptr<juce::XmlElement> save (GenVstAudioProcessor& proc)
    {
        auto root = std::make_unique<juce::XmlElement> (kRootTag);
        if (auto apvtsXml = proc.getValueTreeState().copyState().createXml())
            root->addChildElement (apvtsXml.release());
        return root;
    }

    void restore (GenVstAudioProcessor& proc, const juce::XmlElement& xml)
    {
        // Direct apvts root (pre-Task-10 / unwrapped) — replace state in place.
        if (xml.hasTagName (proc.getValueTreeState().state.getType()))
        {
            proc.getValueTreeState().replaceState (juce::ValueTree::fromXml (xml));
            return;
        }

        // v2 wrapper. The single child holds the apvts XML; defensively pick
        // the first matching one in case future fields land alongside it.
        if (xml.hasTagName (kRootTag))
        {
            for (auto* child : xml.getChildIterator())
            {
                if (child->hasTagName (proc.getValueTreeState().state.getType()))
                {
                    proc.getValueTreeState().replaceState (juce::ValueTree::fromXml (*child));
                    return;
                }
            }
        }
    }
}

MediaCaptureNokiaEffects
========================

The universal app (Win8.1/WP8.1) demonstrates how to use the Nokia Imaging SDK within a Media Foundation Transform to be used as an effect during MediaCapture using a camera.

There are two ways to use this:

1.  You can pass custom strings that represent Filters to the IPropertySet interface that must be added to the AddEffectAsync method.

2.  You can ignore the IPropertySet interface and manually add Filters within the C++ component.  




Using IPropertySet (Method 1)

1.  The first KeyValuePair to be passed to IPropertySet is "filterList" with a List<string> that contains the exact names of all of the Filters you want to use in the order you wish to use them.  
2.  Add another KeyValuePair for each Filter you added in the first step.  This time you will use the filter name as the key and the object will be a simple string that is either empty or contains manual parameters for the Filter.  Enums MUST be entered as ints.

Currently, ONLY LomoFilter is setup to operate this way (and SolarizeFilter partially).  The other filters need to be added in to the C++ component under the GetFilter function in ImagingEffect.cpp.  You can use the LomoFilter section as a template for the rest.


Adding Filters manually to the C++ project (Method 2)

1. Leave the current IPropertySet alone and add Filters to the ApplyImagingFilters function in much the same way you would do in a normal project.  You can remove or comment out the section that parses the filter parameters from IPropertySet.

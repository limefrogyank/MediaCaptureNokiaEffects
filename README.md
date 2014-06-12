MediaCaptureNokiaEffects
========================

The universal app (Win8.1/WP8.1) demonstrates how to use the Nokia Imaging SDK within a Media Foundation Transform to be used as an effect during MediaCapture using a camera.

REQUIRES: Nokia Imaging SDK (use Nuget to add to ALL projects, both WP8/Win8 and C#/C++)

There are two ways to use this:

1.  You can pass a List<IImageProvider> using the IPropertySet interface with the key "IImageProviders".  

2.  You can ignore the IPropertySet interface and manually add an effect pipeline within the C++ component.  




Using IPropertySet (Method 1)

- You can add any number of effects to the List of IImageProviders.  The first effect in the list will have its source property associated with a BitmapImageSource containing the raw video frames being captured.  The last effect in the list will be rendered to a BitmapImage to output.  Except for the first effect, all effects in the list MUST already be connected... i.e. their source properties must have a reference to the preceeding effect. 

Adding Filters manually to the C++ project (Method 2)

- Leave the current IPropertySet alone and add Filters to the ApplyImagingFilters function in much the same way you would do in a normal project.  You can remove or comment out the section that parses the filter parameters from IPropertySet.

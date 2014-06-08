
NuGet has successfully installed the SDK to your project !

Finalizing the installation
===========================
  - Some version of Visual studio may not find the references to the Nokia Imaging SDK that were added to your project by NuGet.  To fix things, simply close your project and reopen it. 
  - Make sure that your project doesn't have "Any CPU" as an "Active solution platform". You will find the instructions how to do this here: http://developer.nokia.com/resources/library/Lumia/nokia-imaging-sdk/adding-libraries-to-the-project.html
  

New Users
=========

If this is your first time with the Nokia Imaging SDK, welcome, we are glad to have you with us! To get you started off on the right foot, take a quick peek at our documentation :   
   http://developer.nokia.com/resources/library/Lumia/nokia-imaging-sdk.html


New in SDK 1.2.115
==================

This a bug fix release, provinding a few important tweaks and fixes to the GifRenderer and the ImageAligner.


New in SDK 1.2.99
=================

Support for Windows Phone 8.1.
New cool filters and enhancements. 
 - ImageAligner : Aligns a series of images that differ by small movements. 
 - GifRenderer : Create GIF images. Create animated gifs. 
 - TargetArea of the BlendFilter : It is now possible to blend images on top of others, defining the targetArea and rotation, with easy to use syntax. Great for stickers! 
 - DelegatingFilter/CustomFilterBase : Allows to build your own custom block based filter, true tile based image processing in a memory efficient way. 


Copyright (c) 2012-2014, Nokia
All rights reserved.






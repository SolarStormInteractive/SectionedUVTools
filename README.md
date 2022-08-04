# SectionedUVTools
A plugin with the material function and tools for splitting static and skeletal meshes to be used in the section UV mapped approach described in Charlies video [here](https://www.youtube.com/watch?v=ncwW5KNQ1Eg).

The idea here is that draw calls are reduced by creating a UV channel which contains the meshes verts placed into sections and a material function which based on the section the vert falls in chooses the appropriate color / texture to use. You only need 1 material slot containing your material using the Sectioned UV material function and the textures / colors each section represents. See the video for details.

A screenshot of 100 characters comprised of 9 draw calls each

<img src="/Screenshots/before_sectioned.png">

The characters reduced to 3 draw calls (two eyes and the sectioned). Savings of about 2ms with a substantial draw call reduction.

<img src="/Screenshots/after_sectioned.png">

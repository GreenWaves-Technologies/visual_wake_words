# Visual Wake Words for GAP9

In this project you can run visual wake words task on GAP9 chip. The NNs used for this task have been taken from open source projects:

	- [MobileNet](https://github.com/mlcommons/tiny/tree/master/benchmark/training/visual_wake_words/trained_models): used for TinyML benchmark for best speed
	- [MIT Proxyless NAS](https://blog.tensorflow.org/2019/10/visual-wake-words-with-tensorflow-lite_30.html): for best accuracy 

The model can be chosen via `Kconfig`, NNTool will take the right model path to generate the Autotiler code using the `nntool_generate_model.py` script. Once the Autotiler Model is generated it is compiled and run to generate the final NN GAP code.

The application has 2 operating modes that can be chosen via `Kconfig` described in the following:

## INFERENCE:

In this mode it simply run the NN on samples from files. This mode can emulate the DEMO mode by resizing the image from the CAMERA size (emulating the images coming from a similar camera) by enabling the `INFERENCE_RESIZER` in `Kconfig`. Otherwise no resize will be applied and the images from files are expected of the correct size.

## DEMO:

**NOTE**: Only usable on board using camera OV5647

In this mode the application runs on GAP9 taking inputs from the camera and running inference with the selected NN.

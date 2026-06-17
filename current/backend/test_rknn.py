from rknnlite.api import RKNNLite

MODEL = "models/yolov8.rknn"

rknn = RKNNLite()

print("Loading RKNN model...")
ret = rknn.load_rknn(MODEL)
if ret != 0:
    print("Load failed")
    exit(ret)

print("Initializing RK3588 NPU...")
ret = rknn.init_runtime(core_mask=RKNNLite.NPU_CORE_AUTO)
if ret != 0:
    print("NPU init failed")
    exit(ret)

print("SUCCESS: YOLOv8 running on RK3588 NPU!")

rknn.release()

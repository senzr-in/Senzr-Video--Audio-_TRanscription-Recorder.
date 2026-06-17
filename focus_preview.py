import cv2

cap = cv2.VideoCapture(0)
ok, frame = cap.read()
print("opened:", cap.isOpened(), "read:", ok)

if ok:
    out = "/opt/edge-gateway/frame.jpg"
    cv2.imwrite(out, frame)
    print("saved:", out, "shape:", frame.shape)

cap.release()

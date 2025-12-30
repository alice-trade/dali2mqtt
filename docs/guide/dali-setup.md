# DALI Setup & Commissioning

DaliMQTT allows you to manage the DALI bus directly from the WebUI without external tools.

## 1. Bus Scan
Go to the **DALI Control** tab in the WebUI.
*   Click **Scan Bus**.
*   The bridge will send queries to all 64 short addresses.
*   Found devices will appear in the "Devices" list.

## 2. Initialization (Addressing)
If you have new, unaddressed drivers:
1.  Click **Initialize New Devices**.
2.  **Warning:** This will assign short addresses to any device on the bus that does not currently have one.
3.  The process uses the DALI Random Address algorithm.

## 3. Group Assignment
In the **DALI Control** -> **Group Management** view:
*   You will see a matrix of Devices vs. Groups (0-15).
*   Toggle checkboxes to add/remove devices from DALI groups.
*   Click **Save Changes** to write configuration to the actual DALI ballasts.

## 4. Scene Configuration
In the **Scene Editor**:
1.  Select a Scene ID (0-15).
2.  Set brightness levels for required devices.
3.  Click **Save Scene**. The levels are stored in the device's memory.

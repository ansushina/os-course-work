#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/usb/input.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/workqueue.h>

#define DRIVER_NAME    "tablet_keyboard_driver"
#define DRIVER_LICENSE "GPL"

MODULE_LICENSE(DRIVER_LICENSE);

#define ID_VENDOR_TABLET  0x056a /* Wacom Co. */
#define ID_PRODUCT_TABLET 0x00df /* CTH-670 */

#define USB_PACKET_LEN  64

#define MAX_VALUE 0xFF

#define X_BUTTONS_COUNT 14
#define Y_BUTTONS_COUNT 6

#define X_BUTTON_LEN ( MAX_VALUE / X_BUTTONS_COUNT )
#define Y_BUTTON_LEN ( MAX_VALUE / Y_BUTTONS_COUNT )

struct tablet_data {
    unsigned char     *data;
    dma_addr_t         data_dma;
    struct usb_device *usb_dev;
    struct urb        *irq;
};

typedef struct tablet_data tablet_t;

typedef struct {
  struct work_struct my_work;
  struct urb        *irq;
} my_work_t;

static struct input_dev *keyboard_dev;
static struct workqueue_struct *wq;
static int pressed[Y_BUTTONS_COUNT][X_BUTTONS_COUNT];

static int keys[Y_BUTTONS_COUNT][X_BUTTONS_COUNT] = {
    { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRINT, KEY_INSERT},
    { KEY_ESC, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE },
    { KEY_TAB, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH },
    { KEY_CAPSLOCK, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_ENTER },
    { KEY_LEFTSHIFT, KEY_LEFTSHIFT, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RIGHTSHIFT, KEY_RIGHTSHIFT },
    { KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFTALT, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_SPACE, KEY_RIGHTALT, 0, 0, KEY_RIGHTCTRL },
};

static int extra_keys[2][3] = {
    { KEY_DELETE, KEY_UP, KEY_GRAVE },
    { KEY_LEFT, KEY_DOWN, KEY_RIGHT },
};

static void my_wq_function( struct work_struct *work)
{
    my_work_t *my_work;
    int retval;
    u16 x, y;
    struct urb *urb;
    tablet_t *tablet;
    unsigned char *data;
    int pressed_key;

    my_work = (my_work_t *)work;
    urb = my_work->irq;
    tablet = urb->context;
    data = tablet->data;

    // проверяем что урб успешно выполнился
    if (urb->status != 0) {
        printk(KERN_ERR "%s: %s - urb status is %d\n", DRIVER_NAME, __func__, urb->status);
        return;
    }

    if (data[2] == 0x80) {
        if (data[3] == 0x1) {
              printk(KERN_INFO "%s: left left click\n", DRIVER_NAME);
        }
        else if (data[3] == 0x2) {
              printk(KERN_INFO "%s: left center click\n", DRIVER_NAME);
        }
        else if (data[3] == 0x10) {
              printk(KERN_INFO "%s: right center click\n", DRIVER_NAME);
        }
        else if (data[3] == 0x8) {
              printk(KERN_INFO "%s: rigtn rigth click\n", DRIVER_NAME);

        } 
        else if (data[3] == 0x0 && data[4] == 0x0 && data[5] == 0x0) {
             printk(KERN_INFO "%s: click ends\n", DRIVER_NAME);
        }
        else  {
            int i = 0;
            while (i < 10) {
                printk(KERN_INFO "%s: data[%d] = %x\n", DRIVER_NAME, i,  data[i]);
                i++;
            }
        }
    } 
    else if (data[3] == 0x20) {
        int i;
        int j;
        for (i = 0; i < Y_BUTTONS_COUNT; i++) {
            for (j = 0; j < X_BUTTONS_COUNT; j++)
            {
                if (pressed[i][j]) {   
                    input_report_key(keyboard_dev, keys[i][j], 0);
                    input_sync(keyboard_dev);
                    pressed[i][j] = 0;
                    printk(KERN_INFO "%s: pen leaves %x\n", DRIVER_NAME,keys[i][j]);
                }
            }
            
        } 
    }
    else if (data[3] == 0xc0) {
        x = data[4];
        y = data[5]; 

        pressed_key = keys[y / Y_BUTTON_LEN][x / X_BUTTON_LEN];
        pressed[y / Y_BUTTON_LEN][x / X_BUTTON_LEN] = 1;
        if (pressed_key == 0) {
            pressed_key = extra_keys[y % Y_BUTTON_LEN / (Y_BUTTON_LEN / 2)][x % X_BUTTON_LEN / (X_BUTTON_LEN / 3)];
        }
        printk(KERN_INFO "%s: pressed %x\n", DRIVER_NAME, pressed_key);

        input_report_key(keyboard_dev, pressed_key, 1);
        input_sync(keyboard_dev);
    }

    retval = usb_submit_urb (urb, GFP_ATOMIC);
    if (retval)
        printk(KERN_ERR "%s: %s - usb_submit_urb failed with result %d\n", DRIVER_NAME, __func__, retval);

    kfree(my_work);
}

static void irq_function(struct urb *urb) {
    my_work_t *work = (my_work_t *)kmalloc(sizeof(my_work_t), GFP_KERNEL);
   
    if (work) {
        work->irq = urb; 
        INIT_WORK( (struct work_struct *)work, my_wq_function );
        queue_work( wq, (struct work_struct *)work );
    }
}

static int tablet_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *endpoint; // оконечная точка usb-стройства
    struct usb_device *usb_device; // usb - устройство
    tablet_t *tablet; // внутренние данные
    int error = -ENOMEM, i, j;

    printk(KERN_INFO "%s: probe checking tablet\n", DRIVER_NAME);

    endpoint = &interface->cur_altsetting->endpoint[0].desc;

    // проверим, что поключен верный интерфейс
    // нам нужна только оконечная точка 0x82, так как именно на нее приходят нужные прерывания
    printk(KERN_INFO "%s: endpoint %x\n", DRIVER_NAME, endpoint->bEndpointAddress);
    if (endpoint->bEndpointAddress != 0x82) {
        return -1;
    }

    // СОздание очереди работ

    wq = create_workqueue("workqueue");
    if (wq == NULL) {
        printk(KERN_ERR "%s: allocation workqueue error\n", DRIVER_NAME);
        return -1;
    }

    // Регистрация устройства ввода

    keyboard_dev = input_allocate_device();
    if (keyboard_dev == NULL) {
        printk(KERN_ERR "%s: allocation device error\n", DRIVER_NAME);
        return -1;
    }

    keyboard_dev->name = "virtual keyboard";

    set_bit(EV_KEY, keyboard_dev->evbit);
    for (i = 0; i < Y_BUTTONS_COUNT; ++i) {
        for (j = 0; j < X_BUTTONS_COUNT; ++j) {
            if (keys[i][j] != 0) {
                set_bit(keys[i][j], keyboard_dev->keybit);
            }
        }
    }

    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 3; ++j) {
            set_bit(extra_keys[i][j], keyboard_dev->keybit);
        }
    }
    
    error = input_register_device(keyboard_dev);
    if (error != 0) {
        input_free_device(keyboard_dev);
        printk(KERN_ERR "%s: registration device error\n", DRIVER_NAME);
        return error;
    }

    // Работа с usb-устройством
    usb_device = interface_to_usbdev(interface); 

    tablet = kzalloc(sizeof(tablet_t), GFP_KERNEL);
    tablet->usb_dev = usb_device;

    tablet->data = (unsigned char *)usb_alloc_coherent(usb_device, USB_PACKET_LEN, GFP_KERNEL, &tablet->data_dma);
    if (!tablet->data) {
        input_free_device(keyboard_dev);
        kfree(tablet);

        printk(KERN_ERR "%s: error when allocate coherent\n", DRIVER_NAME);
        return error;
    }

    tablet->irq = usb_alloc_urb( 0, GFP_KERNEL);
    if (!tablet->irq) {
        input_free_device(keyboard_dev);
        usb_free_coherent(usb_device, USB_PACKET_LEN, tablet->data, tablet->data_dma);
        kfree(tablet);

        printk(KERN_ERR "%s: error when allocate urb\n", DRIVER_NAME);
        return error;
    }

    // заполняем структуру урб
    usb_fill_int_urb(
        tablet->irq, // ссылка на urb
        usb_device, // устройство 
        usb_rcvintpipe(usb_device, endpoint->bEndpointAddress), // адрес оконечной точки
        tablet->data, // данные пакета
        USB_PACKET_LEN, // максимальный размер пакета
        irq_function, // функция обработчика прерывания ->> complete 
        tablet, // контекст
        endpoint->bInterval // интервал между прерываниями
    );

    // отправляем урб на устройство
    usb_submit_urb(tablet->irq, GFP_ATOMIC);

    usb_set_intfdata(interface, tablet);

    printk(KERN_INFO "%s: device is conected\n", DRIVER_NAME);

    return 0;

}

static void tablet_disconnect(struct usb_interface *interface) {
    tablet_t *tablet;
    printk(KERN_INFO "%s: tablet disconnect\n", DRIVER_NAME);

    tablet = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    if (tablet) {
        flush_workqueue(wq);
        destroy_workqueue(wq);
        usb_kill_urb(tablet->irq);
        input_unregister_device(keyboard_dev);
        usb_free_urb(tablet->irq);
        usb_free_coherent(interface_to_usbdev(interface), USB_PACKET_LEN, tablet->data, tablet->data_dma);
        kfree(tablet);

        printk(KERN_INFO "%s: device was disconected\n", DRIVER_NAME);
    }
}

static struct usb_device_id tablet_devices_ids [] = {
    { USB_DEVICE(ID_VENDOR_TABLET, ID_PRODUCT_TABLET) },
    { },
};

MODULE_DEVICE_TABLE(usb, tablet_devices_ids);

static struct usb_driver tablet_driver = {
    .name       = DRIVER_NAME,
    .probe      = tablet_probe,
    .disconnect = tablet_disconnect,
    .id_table   = tablet_devices_ids,
};

static int __init tablet_driver_init(void) {
    // регестрируем usb-усройство
    int result = usb_register(&tablet_driver);
    if (result < 0) {
       printk(KERN_ERR "%s: usb register error\n", DRIVER_NAME);
       return result;
    }

    printk(KERN_INFO "%s: module loaded\n", DRIVER_NAME);
    return 0;
}

static void __exit tablet_driver_exit(void) {
    usb_deregister(&tablet_driver);
    printk(KERN_INFO "%s: module unloaded\n", DRIVER_NAME);
}

module_init(tablet_driver_init);
module_exit(tablet_driver_exit);
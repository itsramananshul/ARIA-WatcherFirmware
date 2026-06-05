#include "view_image_preview.h"
#include "esp_log.h"
#include <mbedtls/base64.h>
#include "esp_jpeg_dec.h"
#include "ui/ui_helpers.h"
#include "util.h"
#include "esp_timer.h"

#define IMAGE_INVOKED_BOXES 10

#define RECTANGLE_COLOR lv_palette_main(LV_PALETTE_RED)


static lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.w = IMG_WIDTH,
    .header.h = IMG_HEIGHT,
    .data_size = IMG_RAM_BUF_SIZE,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static lv_obj_t *ui_image = NULL;
static lv_obj_t *ui_rectangle[IMAGE_INVOKED_BOXES];
static lv_obj_t *ui_class_name[IMAGE_INVOKED_BOXES];

static uint8_t *image_jpeg_buf = NULL;
static uint8_t *image_ram_buf = NULL;

static lv_color_t cls_color[20];

static jpeg_dec_io_t *jpeg_io = NULL;
static jpeg_dec_header_info_t *out_info = NULL;
static jpeg_dec_handle_t jpeg_dec = NULL;

static void classes_color_init()
{
    cls_color[0] = lv_palette_main(LV_PALETTE_RED);
    cls_color[1] = lv_palette_main(LV_PALETTE_YELLOW); 
    cls_color[2] = lv_palette_main(LV_PALETTE_GREEN);
    cls_color[3] = lv_palette_main(LV_PALETTE_BROWN);
    cls_color[4] = lv_palette_main(LV_PALETTE_PINK);
    cls_color[5] = lv_color_hex(0x00a86b);
    cls_color[6] = lv_color_hex(0xfcc200);
    cls_color[7] = lv_color_hex(0x4b0082);
    cls_color[8] = lv_color_hex(0x36648b);
    cls_color[9] = lv_color_hex(0xffc40c);

    cls_color[10] = lv_color_hex(0x444444);
    cls_color[11] = lv_color_hex(0xbe29ec);
    cls_color[12] = lv_color_hex(0xb68fa9);
    cls_color[13] = lv_color_hex(0xa2c4c9);
    cls_color[14] = lv_color_hex(0xadff2f);
    cls_color[15] = lv_color_hex(0x7f1734);
    cls_color[16] = lv_color_hex(0xf7c2c2);
    cls_color[17] = lv_color_hex(0xd0e4e4);
    cls_color[18] = lv_color_hex(0x98f5ff);
    cls_color[19] = lv_color_hex(0xaaf0d1);
}

static int jpeg_decoder_init(void)
{
    esp_err_t ret = ESP_OK;
    jpeg_dec_config_t config = { .output_type = JPEG_RAW_TYPE_RGB565_BE, .rotate = JPEG_ROTATE_0D };
    
    jpeg_dec = jpeg_dec_open(&config);
    if (jpeg_dec == NULL) {
        return ESP_FAIL;
    }

    jpeg_io = heap_caps_malloc(sizeof(jpeg_dec_io_t), MALLOC_CAP_SPIRAM);
    if (jpeg_io == NULL) {
        jpeg_dec_close(jpeg_dec);
        return ESP_FAIL;
    }
    memset(jpeg_io, 0, sizeof(jpeg_dec_io_t));

    out_info = heap_caps_aligned_alloc(16, sizeof(jpeg_dec_header_info_t), MALLOC_CAP_SPIRAM);
    if (out_info == NULL) {
        heap_caps_free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return ESP_FAIL;
    }
    memset(out_info, 0, sizeof(jpeg_dec_header_info_t));

    return ret;
}

// call it to free jpeg decoder mem
static void jpeg_decoder_deinit(void)
{
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;
    }

    if (jpeg_io) {
        heap_caps_free(jpeg_io);
        jpeg_io = NULL;
    }

    if (out_info) {
        heap_caps_free(out_info);
        out_info = NULL;
    }
}

#ifdef CONFIG_CAMERA_DISPLAY_MIRROR_X
//15ms
static  HEAP_IRAM_ATTR void mirror_x(uint16_t *image_ram_buf)
{
    uint16_t *p_a;
    uint16_t *p_b;
    for (int y = 0; y < IMG_HEIGHT; y++) {
        uint16_t *row = image_ram_buf + y * IMG_WIDTH;
        for (int x = 0; x < IMG_WIDTH / 2; x++) {
            p_a = (uint16_t *)&row[x];
            p_b = (uint16_t *)&row[(IMG_WIDTH - 1 - x)];
            *p_a = *p_a ^ *p_b;
            *p_b = *p_a ^ *p_b;
            *p_a = *p_a ^ *p_b;
        }
    }
}
#endif

static int esp_jpeg_decoder_one_picture(uint8_t *input_buf, int len, uint8_t *output_buf)
{
    esp_err_t ret = ESP_OK;

    if (!jpeg_dec || !jpeg_io || !out_info) {
        return ESP_FAIL;
    }

    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret < 0) {
        return ret;
    }

    jpeg_io->outbuf = output_buf;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = input_buf + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    return ret;
}


int view_image_preview_init(lv_obj_t *ui_screen)
{
    int ret = jpeg_decoder_init();
    if (ret != ESP_OK) {
        return ret;
    }

    image_jpeg_buf = psram_malloc(IMG_JPEG_BUF_SIZE);
    assert(image_jpeg_buf);

    //must be 16 byte aligned
    image_ram_buf = heap_caps_aligned_alloc(16, IMG_RAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    assert(image_ram_buf);

    ui_image = lv_img_create(ui_screen);
    lv_obj_set_align(ui_image, LV_ALIGN_CENTER);

    for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
    {
        ui_rectangle[i] = lv_obj_create(ui_screen);
        lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_EVENT_BUBBLE);

        ui_class_name[i] = lv_label_create(ui_screen);
        lv_obj_set_width(ui_class_name[i], LV_SIZE_CONTENT);  /// 1
        lv_obj_set_height(ui_class_name[i], LV_SIZE_CONTENT); /// 1
        lv_obj_set_style_text_font(ui_class_name[i], &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
    }
    classes_color_init();
    return 0;
}


int view_image_preview_flush(struct tf_module_ai_camera_preview_info *p_info)
{
    int ret = 0;
    int64_t start = 0, end = 0;
    size_t output_len = 0;
    if (ui_image == NULL)
    {
        return 0;
    }
    
    if( lv_scr_act() != ui_Page_ViewLive) {
        return 0;
    }

    ret = mbedtls_base64_decode(image_jpeg_buf, IMG_JPEG_BUF_SIZE, &output_len, p_info->img.p_buf, p_info->img.len);
    if (ret != 0 || output_len == 0)
    {
        ESP_LOGE("view", "Failed to decode base64: %d", ret);
        return ret;
    }

    start = esp_timer_get_time();
    ret = esp_jpeg_decoder_one_picture(image_jpeg_buf, output_len, image_ram_buf);
    if (ret != ESP_OK) {
        ESP_LOGE("view", "Failed to decode jpeg: %d", ret);
        return ret;
    }
    end = esp_timer_get_time();
    // printf("decode time:%lld ms\r\n", (end - start) / 1000);

#ifdef CONFIG_CAMERA_DISPLAY_MIRROR_X
    start = esp_timer_get_time();
    mirror_x((uint16_t *)image_ram_buf);
    end = esp_timer_get_time();
    // printf("mirror time:%lld ms\r\n", (end - start) / 1000);
#endif

    img_dsc.data = image_ram_buf;
    lv_img_set_src(ui_image, &img_dsc);

    if (!p_info->inference.is_valid) {
        for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
        {
            lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
        }
        return 0;
    }

    switch (p_info->inference.type)
    {
        case INFERENCE_TYPE_BOX:
            for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
            {
                if (i < p_info->inference.cnt)
                {
                    int x = 0;
                    int y = 0;
                    int w = 0;
                    int h = 0;
                    sscma_client_box_t *p_box = (sscma_client_box_t *)p_info->inference.p_data;
#ifdef CONFIG_CAMERA_DISPLAY_MIRROR_X
                    x = IMG_WIDTH - p_box[i].x; //x mirror
#else
                    x = p_box[i].x;
#endif
                    
                    y = p_box[i].y;
                    w = p_box[i].w;
                    h = p_box[i].h;

                    x = x - w / 2;
                    y = y - h / 2;

                    if (x < 0)
                    {
                        x = 0;
                    }

                    if (y < 0)
                    {
                        y = 0;
                    }

                    char *p_class_name = "unknown";
                    if(  p_info->inference.classes[p_box[i].target] != NULL) {
                        p_class_name = p_info->inference.classes[p_box[i].target];
                    }

                    lv_color_t color = cls_color[p_box[i].target];

                    lv_obj_set_pos(ui_rectangle[i], x, y);
                    lv_obj_set_size(ui_rectangle[i], w, h);
                    lv_obj_set_style_border_color(ui_rectangle[i], color, 0);
                    lv_obj_set_style_border_width(ui_rectangle[i], 4, 0);
                    lv_obj_set_style_bg_opa(ui_rectangle[i], LV_OPA_TRANSP, 0);
                    lv_obj_clear_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);

                    // name
                    char buf1[32];
                    lv_snprintf(buf1, sizeof(buf1), "%s:%d", p_class_name, p_box[i].score);

                    lv_obj_set_pos(ui_class_name[i], x, (y - 10) < 0 ? 0 : (y - 10));
                    lv_label_set_text(ui_class_name[i], buf1);
                    lv_obj_set_style_bg_color(ui_class_name[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(ui_class_name[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_clear_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
                else
                {
                    lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            break;

        case INFERENCE_TYPE_CLASS:
            for (size_t i = 0; i < IMAGE_INVOKED_BOXES; i++)
            {
                if (i < p_info->inference.cnt)
                {
                    sscma_client_class_t *p_class = (sscma_client_class_t *)p_info->inference.p_data;
                    
                    char *p_class_name = "unknown";
                    if(  p_info->inference.classes[p_class[i].target] != NULL) {
                        p_class_name = p_info->inference.classes[p_class[i].target];
                    }
                    
                    lv_color_t color = cls_color[p_class[i].target];
                    char buf1[32];
                    lv_snprintf(buf1, sizeof(buf1), "%s:%d", p_class_name, p_class[i].score);
                    
                    
                    lv_obj_set_pos(ui_class_name[i], 60, 60 + i*40);
                    lv_label_set_text(ui_class_name[i], buf1);
                    lv_obj_set_style_bg_color(ui_class_name[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(ui_class_name[i], 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_clear_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
                else
                {
                    lv_obj_add_flag(ui_rectangle[i], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ui_class_name[i], LV_OBJ_FLAG_HIDDEN);
                }
            }

            break;

        default:
            break;
    }

    return 0;
}

static lv_obj_t *black_screen = NULL;

static void create_black_screen_obj()
{
    black_screen = lv_obj_create(lv_scr_act());

    lv_obj_set_size(black_screen, 412, 412);
    lv_obj_align(black_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(black_screen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(black_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(black_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void view_image_black_flush()
{
    _ui_screen_change(&black_screen, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &create_black_screen_obj);
}


// return 0 check success
int view_image_check(uint8_t *p_buf, size_t len, size_t ram_buf_len)
{
    int ret = 0;
    size_t output_len = 0;
    int64_t start = 0, end = 0;
    uint8_t* p_jpeg_buf = NULL;
     uint8_t* p_ram_buf = NULL;  
    if (p_buf == NULL || len == 0 || ram_buf_len == 0)
    {
        return -1;
    }
    start = esp_timer_get_time();

    p_jpeg_buf = psram_malloc(len); //base64 decode buf,  len / (3 * 4)
    if (p_jpeg_buf == NULL )
    {
        ESP_LOGW("view", "psram_malloc failed: %d", len);
        goto err;
    }
    p_ram_buf = heap_caps_aligned_alloc(16, ram_buf_len, MALLOC_CAP_SPIRAM);
    if ( p_ram_buf == NULL)
    {
        ESP_LOGW("view", "psram_malloc failed: %d", ram_buf_len);
        goto err;
    }
    
    ret = mbedtls_base64_decode(p_jpeg_buf, len, &output_len, p_buf, len);
    if (ret != 0 || output_len == 0)
    {
        ESP_LOGE("view", "Failed to decode base64: %d", ret);
        ret = -1;
        goto err;
    }
    
    ret = esp_jpeg_decoder_one_picture(p_jpeg_buf, output_len, p_ram_buf);
    if (ret != ESP_OK) {
        ESP_LOGE("view", "Failed to decode jpeg: %d", ret);
        goto err;
    }
    end = esp_timer_get_time();
    printf("decode time:%lld ms\r\n", (end - start) / 1000);

err:
    if (p_jpeg_buf) {
        free(p_jpeg_buf);
    }
    if( p_ram_buf) {
        free(p_ram_buf);
    }
    return ret;
}
use oxipng::{BitDepth, ColorType, Options, RawImage, StripChunks};

#[repr(C)]
pub struct Buffer {
    pub data: *mut u8,
    pub size: usize,
}

#[no_mangle]
pub unsafe extern "C" fn oxi_encode(data: *const u8, size: usize, w: u32, h: u32, n: u32) -> Buffer {
    let color = match n {
        3 => ColorType::RGB { transparent_color: None },
        4 => ColorType::RGBA,
        _ => return Buffer { data: std::ptr::null_mut(), size: 0 },
    };
    let mut options = Options::from_preset(4);
    options.optimize_alpha = false;
    options.interlace = Some(false);
    options.strip = StripChunks::All;
    let input = std::slice::from_raw_parts(data, size).to_vec();
    let image = RawImage::new(w, h, color, BitDepth::Eight, input).unwrap();
    let output = image.create_optimized_png(&options).unwrap().into_boxed_slice();
    let result = Buffer { data: output.as_ptr() as *mut u8, size: output.len() };
    std::mem::forget(output);
    result
}

#[no_mangle]
pub unsafe extern "C" fn oxi_free(data: *mut u8, size: usize) {
    drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(data, size)));
}

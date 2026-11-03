/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/linux_document.h>
#include <rmrender/linux_marks.h>

int main(void) {
    rm_page page = {0};
    rmr_page_marks *marks = nullptr;
    if (rmr_linux_page_marks(&page, (rmr_point){100, 100}, (rmr_rect){0, 0, 100, 100}, (rm_str){0},
                             nullptr, nullptr, &marks, nullptr) != RMR_OK)
        return 1;
    int result = !rmm_page_is_empty(rmr_page_marks_view(marks));
    rmr_page_marks_free(marks);
    rmr_plan *plan = nullptr;
    if (rmr_linux_page_plan(&page, (rmr_point){100, 100}, RMR_EDGE_RIBBON, 0, nullptr, nullptr,
                            nullptr, &plan, nullptr) != RMR_OK)
        return 2;
    const rmr_plan *plans[] = {plan};
    rmr_buffer pdf = {0};
    if (rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr) != RMR_OK)
        result = 3;
    else if (!pdf.bytes || !pdf.size)
        result = 4;
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
    return result;
}

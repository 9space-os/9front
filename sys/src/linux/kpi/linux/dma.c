// SPDX-License-Identifier: MIT
/*
 * LinuxKPI for 9front: DMA mapping without an IOMMU: the device reaches
 * memory at its physical address; coherent memory is a run of pages
 * from the page pool.  nfront: within the device's DMA mask: the pool
 * gives only pages below it (page.c); one device per process, so the
 * mask is the process's.
 */
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/mm.h>

int
kpi_dma_set_mask(struct device *d, u64 mask)
{
	if (mask < DMA_BIT_MASK(32))
		return -EIO;	/* the pool reaches no lower */
	kpi_dma_limit = mask;
	if (d)
		d->coherent_dma_mask = mask;
	return 0;
}

/* nfront: memory the device reaches (a page of the pool before the mask was narrowed may not) */
static int
reachable(phys_addr_t pa, size_t size)
{
	return pa + size - 1 <= kpi_dma_limit;
}

int
dma_map_sg_attrs(struct device *d, struct scatterlist *sgl, int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		/* a page of the heap has no address for the device (page.c) */
		if (WARN_ON_ONCE(!sg_page(sg)->kpi_chunk))
			return 0;
		if (WARN_ON_ONCE(!reachable(sg_phys(sg), sg->length)))
			return 0;
		sg->dma_address = sg_phys(sg);
		sg->dma_length = sg->length;
	}
	return nents;
}

int
dma_map_sgtable(struct device *d, struct sg_table *t, enum dma_data_direction dir, unsigned long attrs)
{
	int n = dma_map_sg_attrs(d, t->sgl, t->orig_nents, dir, attrs);

	if (n <= 0)
		return -EINVAL;
	t->nents = n;
	return 0;
}

/* a run of the pool's pages is contiguous for the device too (page.c) */
dma_addr_t
dma_map_single_attrs(struct device *d, void *p, size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	struct page *pg = virt_to_page(p);

	if (WARN_ON_ONCE(!pg || !pg->kpi_chunk))
		return DMA_MAPPING_ERROR;
	if (WARN_ON_ONCE(!reachable(page_to_phys(pg) + offset_in_page(p), size)))
		return DMA_MAPPING_ERROR;
	return page_to_phys(pg) + offset_in_page(p);
}

void *
dma_alloc_attrs(struct device *d, size_t size, dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	struct page *p = alloc_pages(gfp | __GFP_ZERO, get_order(size));

	if (!p)
		return NULL;
	*handle = page_to_phys(p);
	return page_address(p);
}

void
dma_free_attrs(struct device *d, size_t size, void *cpu, dma_addr_t handle, unsigned long attrs)
{
	if (cpu)
		__free_pages(virt_to_page(cpu), get_order(size));
}

void *dma_alloc_coherent(struct device *d, size_t size, dma_addr_t *handle, gfp_t gfp) { return dma_alloc_attrs(d, size, handle, gfp, 0); }
void dma_free_coherent(struct device *d, size_t size, void *cpu, dma_addr_t handle) { dma_free_attrs(d, size, cpu, handle, 0); }

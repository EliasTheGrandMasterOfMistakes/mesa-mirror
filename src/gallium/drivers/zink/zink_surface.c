/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_context.h"
#include "zink_format.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"
#include "zink_kopper.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

VkImageViewCreateInfo
create_ivci(struct zink_screen *screen,
            struct zink_resource *res,
            const struct pipe_surface *templ,
            enum pipe_texture_target target)
{
   VkImageViewCreateInfo ivci;
   /* zero holes since this is hashed */
   memset(&ivci, 0, sizeof(VkImageViewCreateInfo));
   ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   ivci.image = res->obj->image;

   switch (target) {
   case PIPE_TEXTURE_1D:
      ivci.viewType = res->need_2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D;
      break;

   case PIPE_TEXTURE_1D_ARRAY:
      ivci.viewType = res->need_2D ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
      break;

   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      break;

   case PIPE_TEXTURE_2D_ARRAY:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      break;

   case PIPE_TEXTURE_CUBE:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;

   case PIPE_TEXTURE_CUBE_ARRAY:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      break;

   case PIPE_TEXTURE_3D:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;

   default:
      unreachable("unsupported target");
   }

   ivci.format = res->base.b.format == PIPE_FORMAT_A8_UNORM ? res->format : zink_get_format(screen, templ->format);
   assert(ivci.format != VK_FORMAT_UNDEFINED);

   /* TODO: it's currently illegal to use non-identity swizzles for framebuffer attachments,
    * but if that ever changes, this will be useful
   const struct util_format_description *desc = util_format_description(templ->format);
   ivci.components.r = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_X));
   ivci.components.g = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Y));
   ivci.components.b = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Z));
   ivci.components.a = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_W));
   */
   ivci.components.r = VK_COMPONENT_SWIZZLE_R;
   ivci.components.g = VK_COMPONENT_SWIZZLE_G;
   ivci.components.b = VK_COMPONENT_SWIZZLE_B;
   ivci.components.a = VK_COMPONENT_SWIZZLE_A;

   ivci.subresourceRange.aspectMask = res->aspect;
   ivci.subresourceRange.baseMipLevel = templ->level;
   ivci.subresourceRange.levelCount = 1;
   ivci.subresourceRange.baseArrayLayer = templ->first_layer;
   ivci.subresourceRange.layerCount = 1 + templ->last_layer - templ->first_layer;
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.baseArrayLayer == 0);
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.layerCount == 1);
   /* ensure cube image types get clamped to 2D/2D_ARRAY as expected for partial views */
   ivci.viewType = zink_surface_clamp_viewtype(ivci.viewType, templ->first_layer, templ->last_layer, res->base.b.array_size);

   return ivci;
}

static void
init_pipe_surface_info(struct pipe_context *pctx, struct pipe_surface *psurf, const struct pipe_surface *templ, const struct pipe_resource *pres)
{
   unsigned int level = templ->level;
   psurf->context = pctx;
   psurf->format = templ->format;
   psurf->nr_samples = templ->nr_samples;
   psurf->level = level;
   psurf->first_layer = templ->first_layer;
   psurf->last_layer = templ->last_layer;
}

static void
apply_view_usage_for_format(struct zink_screen *screen, struct zink_resource *res, struct zink_surface *surface, enum pipe_format format, VkImageViewCreateInfo *ivci)
{
   VkFormatFeatureFlags feats = res->linear ?
                                zink_get_format_props(screen, format)->linearTilingFeatures :
                                zink_get_format_props(screen, format)->optimalTilingFeatures;
   VkImageUsageFlags attachment = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
   surface->usage_info.usage = res->obj->vkusage & ~attachment;
   if (res->obj->modifier_aspect) {
      feats = res->obj->vkfeats;
      /* intersect format features for current modifier */
      for (unsigned i = 0; i < screen->modifier_props[format].drmFormatModifierCount; i++) {
         if (res->obj->modifier == screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifier)
            feats &= screen->modifier_props[format].pDrmFormatModifierProperties[i].drmFormatModifierTilingFeatures;
      }
   }
   /* if the format features don't support framebuffer attachment, use VkImageViewUsageCreateInfo to remove it */
   if ((res->obj->vkusage & attachment) &&
       !(feats & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
      ivci->pNext = &surface->usage_info;
   }
}

static struct zink_surface *
create_surface(struct pipe_context *pctx,
               struct pipe_resource *pres,
               const struct pipe_surface *templ,
               VkImageViewCreateInfo *ivci,
               bool actually)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);

   struct zink_surface *surface = CALLOC_STRUCT(zink_surface);
   if (!surface)
      return NULL;

   surface->usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
   surface->usage_info.pNext = NULL;
   apply_view_usage_for_format(screen, res, surface, templ->format, ivci);

   pipe_resource_reference(&surface->base.texture, pres);
   pipe_reference_init(&surface->base.reference, 1);
   init_pipe_surface_info(pctx, &surface->base, templ, pres);
   surface->obj = zink_resource(pres)->obj;

   if (!actually)
      return surface;
   assert(ivci->image);
   VkResult result = VKSCR(CreateImageView)(screen->dev, ivci, NULL,
                                            &surface->image_view);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateImageView failed (%s)", vk_Result_to_str(result));
      FREE(surface);
      return NULL;
   }

   return surface;
}

static uint32_t
hash_ivci(const void *key)
{
   return _mesa_hash_data((char*)key + offsetof(VkImageViewCreateInfo, flags), sizeof(VkImageViewCreateInfo) - offsetof(VkImageViewCreateInfo, flags));
}

static struct zink_surface *
do_create_surface(struct pipe_context *pctx, struct pipe_resource *pres, const struct pipe_surface *templ, VkImageViewCreateInfo *ivci, uint32_t hash, bool actually)
{
   /* create a new surface */
   struct zink_surface *surface = create_surface(pctx, pres, templ, ivci, actually);
   /* only transient surfaces have nr_samples set */
   surface->base.nr_samples = zink_screen(pctx->screen)->info.have_EXT_multisampled_render_to_single_sampled ? templ->nr_samples : 0;
   surface->hash = hash;
   surface->ivci = *ivci;
   return surface;
}

/* get a cached surface for a shader descriptor */
struct zink_surface *
zink_get_surface(struct zink_context *ctx,
            struct pipe_resource *pres,
            const struct pipe_surface *templ,
            VkImageViewCreateInfo *ivci)
{
   struct zink_surface *surface = NULL;
   struct zink_resource *res = zink_resource(pres);
   uint32_t hash = hash_ivci(ivci);

   simple_mtx_lock(&res->surface_mtx);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, hash, ivci);

   if (!entry) {
      /* create a new surface, but don't actually create the imageview if mutable isn't set and the format is different;
       * mutable will be set later and the imageview will be filled in
       */
      bool actually = !zink_format_needs_mutable(pres->format, templ->format) || (pres->bind & ZINK_BIND_MUTABLE);
      surface = do_create_surface(&ctx->base, pres, templ, ivci, hash, actually);
      entry = _mesa_hash_table_insert_pre_hashed(&res->surface_cache, hash, &surface->ivci, surface);
      if (!entry) {
         simple_mtx_unlock(&res->surface_mtx);
         return NULL;
      }

      surface = entry->data;
   } else {
      surface = entry->data;
      p_atomic_inc(&surface->base.reference.count);
   }
   simple_mtx_unlock(&res->surface_mtx);

   return surface;
}

/* this is the context hook, so only zink_ctx_surfaces will reach it */
static void
zink_surface_destroy(struct pipe_context *pctx,
                     struct pipe_surface *psurface)
{
   /* ensure this gets repopulated if another transient surface is created */
   struct zink_resource *res = zink_resource(psurface->texture);
   if (res->transient)
      res->transient->valid = false;
   zink_destroy_surface(zink_screen(pctx->screen), psurface);
}

/* this the context hook that returns a zink_ctx_surface */
static struct pipe_surface *
zink_create_surface(struct pipe_context *pctx,
                    struct pipe_resource *pres,
                    const struct pipe_surface *templ)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(pres);
   struct zink_screen *screen = zink_screen(pctx->screen);
   bool is_array = templ->last_layer != templ->first_layer;
   enum pipe_texture_target target_2d[] = {PIPE_TEXTURE_2D, PIPE_TEXTURE_2D_ARRAY};
   if (!res->obj->dt && zink_format_needs_mutable(pres->format, templ->format)) {
      /*
         VUID-VkImageViewCreateInfo-image-07072
         If image was created with the VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT flag and
         format is a non-compressed format, the levelCount and layerCount members of
         subresourceRange must both be 1

         ...but this is allowed with a maintenance6 property
       */
      if (util_format_is_compressed(pres->format) && templ->first_layer != templ->last_layer &&
          (!screen->info.have_KHR_maintenance6 || !screen->info.maint6_props.blockTexelViewCompatibleMultipleLayers))
         return NULL;
      
      /* mutable not set by default */
      if (!(res->base.b.bind & ZINK_BIND_MUTABLE))
         zink_resource_object_init_mutable(ctx, res);
   }

   if (!zink_get_format(screen, templ->format))
      return NULL;

   VkImageViewCreateInfo ivci = create_ivci(screen, res, templ,
                                            pres->target == PIPE_TEXTURE_3D ? target_2d[is_array] : pres->target);

   struct zink_surface *surface = NULL;
   if (res->obj->dt) {
      /* don't cache swapchain surfaces. that's weird. */
      surface = do_create_surface(pctx, pres, templ, &ivci, 0, false);
      if (unlikely(!surface)) {
         mesa_loge("ZINK: failed do_create_surface!");
         return NULL;
      }

      surface->is_swapchain = true;
   } else {
      surface = zink_get_surface(zink_context(pctx), pres, templ, &ivci);
      if (unlikely(!surface)) {
         mesa_loge("ZINK: failed to get surface!");
         return NULL;
      }
   }

   return &surface->base;
}

struct zink_surface *
zink_create_transient_surface(struct zink_context *ctx, struct zink_surface *surf, unsigned nr_samples)
{
   struct zink_resource *res = zink_resource(surf->base.texture);
   struct zink_resource *transient = res->transient;
   assert(nr_samples > 1);
   if (!res->transient) {
      /* transient fb attachment: not cached */
      struct pipe_resource rtempl = *surf->base.texture;
      rtempl.nr_samples = nr_samples;
      rtempl.bind |= ZINK_BIND_TRANSIENT;
      res->transient = zink_resource(ctx->base.screen->resource_create(ctx->base.screen, &rtempl));
      transient = res->transient;
      if (unlikely(!transient)) {
         mesa_loge("ZINK: failed to create transient resource!");
         return NULL;
      }
   }

   VkImageViewCreateInfo ivci = surf->ivci;
   ivci.image = transient->obj->image;
   ivci.pNext = NULL;
   return zink_get_surface(ctx, &transient->base.b, &surf->base, &ivci);
}

void
zink_destroy_surface(struct zink_screen *screen, struct pipe_surface *psurface)
{
   struct zink_surface *surface = zink_surface(psurface);
   struct zink_resource *res = zink_resource(psurface->texture);
   if ((!psurface->nr_samples || screen->info.have_EXT_multisampled_render_to_single_sampled) && !surface->is_swapchain) {
      simple_mtx_lock(&res->surface_mtx);
      if (psurface->reference.count) {
         /* a different context got a cache hit during deletion: this surface is alive again */
         simple_mtx_unlock(&res->surface_mtx);
         return;
      }
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci);
      assert(he);
      assert(he->data == surface);
      _mesa_hash_table_remove(&res->surface_cache, he);
      simple_mtx_unlock(&res->surface_mtx);
   }
   /* this surface is dead now */
   simple_mtx_lock(&res->obj->view_lock);
   /* imageviews are never destroyed directly to ensure lifetimes for in-use surfaces */
   if (surface->is_swapchain) {
      for (unsigned i = 0; i < surface->swapchain_size; i++)
         util_dynarray_append(&res->obj->views, VkImageView, surface->swapchain[i]);
      free(surface->swapchain);
   } else
      util_dynarray_append(&res->obj->views, VkImageView, surface->image_view);
   simple_mtx_unlock(&res->obj->view_lock);
   pipe_resource_reference(&psurface->texture, NULL);
   FREE(surface);
}

/* this is called when a surface is rebound for mutable/storage use */
bool
zink_rebind_surface(struct zink_context *ctx, struct pipe_surface **psurface)
{
   struct zink_surface *surface = zink_surface(*psurface);
   struct zink_resource *res = zink_resource((*psurface)->texture);
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (surface->obj == res->obj)
      return false;
   assert(!res->obj->dt);
   VkImageViewCreateInfo ivci = surface->ivci;
   ivci.image = res->obj->image;
   uint32_t hash = hash_ivci(&ivci);

   simple_mtx_lock(&res->surface_mtx);
   struct hash_entry *new_entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, hash, &ivci);
   if (new_entry) {
      /* reuse existing surface; old one will be cleaned up naturally */
      struct zink_surface *new_surface = new_entry->data;
      simple_mtx_unlock(&res->surface_mtx);
      pipe_surface_reference(psurface, &new_surface->base);
      return true;
   }
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci);
   assert(entry);
   _mesa_hash_table_remove(&res->surface_cache, entry);
   VkImageView image_view;
   apply_view_usage_for_format(screen, res, surface, surface->base.format, &ivci);
   VkResult result = VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &image_view);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: failed to create new imageview (%s)", vk_Result_to_str(result));
      simple_mtx_unlock(&res->surface_mtx);
      return false;
   }
   surface->hash = hash;
   surface->ivci = ivci;
   entry = _mesa_hash_table_insert_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci, surface);
   assert(entry);
   simple_mtx_lock(&res->obj->view_lock);
   util_dynarray_append(&res->obj->views, VkImageView, surface->image_view);
   simple_mtx_unlock(&res->obj->view_lock);
   surface->image_view = image_view;
   surface->obj = zink_resource(surface->base.texture)->obj;
   simple_mtx_unlock(&res->surface_mtx);
   return true;
}

void
zink_context_surface_init(struct pipe_context *context)
{
   context->create_surface = zink_create_surface;
   context->surface_destroy = zink_surface_destroy;
}

/* must be called before a swapchain image is used to ensure correct imageview is used */
void
zink_surface_swapchain_update(struct zink_context *ctx, struct zink_surface *surface)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_resource *res = zink_resource(surface->base.texture);
   struct kopper_displaytarget *cdt = res->obj->dt;
   if (!cdt)
      return; //dead swapchain
   if (cdt->swapchain != surface->dt_swapchain) {
      /* new swapchain: clear out previous swapchain imageviews/array and setup a new one;
       * old views will be pruned normally in zink_batch or on object destruction
       */
      simple_mtx_lock(&res->obj->view_lock);
      for (unsigned i = 0; i < surface->swapchain_size; i++)
         util_dynarray_append(&res->obj->views, VkImageView, surface->swapchain[i]);
      simple_mtx_unlock(&res->obj->view_lock);
      free(surface->swapchain);
      surface->swapchain_size = cdt->swapchain->num_images;
      surface->swapchain = calloc(surface->swapchain_size, sizeof(VkImageView));
      if (!surface->swapchain) {
         mesa_loge("ZINK: failed to allocate surface->swapchain!");
         return;
      }
      surface->dt_swapchain = cdt->swapchain;
   }
   if (!surface->swapchain[res->obj->dt_idx]) {
      /* no current swapchain imageview exists: create it */
      assert(res->obj->image && cdt->swapchain->images[res->obj->dt_idx].image == res->obj->image);
      surface->ivci.image = res->obj->image;
      assert(surface->ivci.image);
      VKSCR(CreateImageView)(screen->dev, &surface->ivci, NULL, &surface->swapchain[res->obj->dt_idx]);
   }
   /* the current swapchain imageview is now the view for the current swapchain image */
   surface->image_view = surface->swapchain[res->obj->dt_idx];
}

void
zink_surface_resolve_init(struct zink_screen *screen, struct zink_resource *res, enum pipe_format format)
{
   if (res->surface)
      return;
   struct pipe_surface tmpl = {0};
   tmpl.format = format;
   zink_screen_lock_context(screen);
   res->surface = screen->copy_context->base.create_surface(&screen->copy_context->base, &res->base.b, &tmpl);
   zink_screen_unlock_context(screen);
   /* delete extra ref: the resource controls the surface lifetime, not the other way around */
   struct pipe_resource *pres = &res->base.b;
   pipe_resource_reference(&pres, NULL);
}

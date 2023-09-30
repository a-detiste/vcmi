/*
 * CMapOverview.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "CMapOverview.h"

#include "../lobby/SelectionTab.h"

#include <vstd/DateUtils.h>

#include "../gui/CGuiHandler.h"
#include "../gui/WindowHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"
#include "../render/CAnimation.h"
#include "../render/Canvas.h"
#include "../render/IImage.h"
#include "../render/IRenderHandler.h"
#include "../render/Graphics.h"

#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/campaign/CampaignState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapService.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/mapping/MapFormat.h"
#include "../../lib/TerrainHandler.h"
#include "../../lib/filesystem/Filesystem.h"

#include "../../lib/serializer/BinaryDeserializer.h"
#include "../../lib/StartInfo.h"
#include "../../lib/rmg/CMapGenOptions.h"

CMapOverview::CMapOverview(std::string mapName, std::string fileName, std::string date, ResourcePath resource, ESelectionScreen tabType)
	: CWindowObject(BORDERED | RCLICK_POPUP), resource(resource), mapName(mapName), fileName(fileName), date(date), tabType(tabType)
{

	OBJ_CONSTRUCTION_CAPTURING_ALL_NO_DISPOSE;

	const JsonNode config(JsonPath::builtin("config/widgets/mapOverview.json"));
	pos = Rect(0, 0, config["items"][0]["rect"]["w"].Integer(), config["items"][0]["rect"]["h"].Integer());

	widget = std::make_shared<CMapOverviewWidget>(*this);

	updateShadow();

	center(GH.getCursorPosition()); //center on mouse
#ifdef VCMI_MOBILE
	moveBy({0, -pos.h / 2});
#endif
	fitToScreen(10);
}

Canvas CMapOverview::CMapOverviewWidget::createMinimapForLayer(std::unique_ptr<CMap> & map, int layer) const
{
	Canvas canvas = Canvas(Point(map->width, map->height));

	for (int y = 0; y < map->height; ++y)
		for (int x = 0; x < map->width; ++x)
		{
			TerrainTile & tile = map->getTile(int3(x, y, layer));

			ColorRGBA color = tile.terType->minimapUnblocked;
			if (tile.blocked && (!tile.visitable))
				color = tile.terType->minimapBlocked;

			if(drawPlayerElements)
				// if object at tile is owned - it will be colored as its owner
				for (const CGObjectInstance *obj : tile.blockingObjects)
				{
					PlayerColor player = obj->getOwner();
					if(player == PlayerColor::NEUTRAL)
					{
						color = graphics->neutralColor;
						break;
					}
					if (player.isValidPlayer())
					{
						color = graphics->playerColors[player.getNum()];
						break;
					}
				}

			canvas.drawPoint(Point(x, y), color);
		}
	
	return canvas;
}

std::vector<Canvas> CMapOverview::CMapOverviewWidget::createMinimaps(ResourcePath resource) const
{
	std::vector<Canvas> ret = std::vector<Canvas>();

	CMapService mapService;
	std::unique_ptr<CMap> map;
	try
	{
		map = mapService.loadMap(resource);
	}
	catch (...)
	{
		return ret;
	}

	return createMinimaps(map);
}

std::vector<Canvas> CMapOverview::CMapOverviewWidget::createMinimaps(std::unique_ptr<CMap> & map) const
{
	std::vector<Canvas> ret = std::vector<Canvas>();

	for(int i = 0; i < (map->twoLevel ? 2 : 1); i++)
		ret.push_back(createMinimapForLayer(map, i));

	return ret;
}

std::shared_ptr<CPicture> CMapOverview::CMapOverviewWidget::buildDrawMinimap(const JsonNode & config) const
{
	logGlobal->debug("Building widget drawMinimap");

	auto rect = readRect(config["rect"]);
	auto id = config["id"].Integer();

	if(id >= minimaps.size())
		return nullptr;

	Canvas canvasScaled = Canvas(Point(rect.w, rect.h));
	canvasScaled.drawScaled(minimaps[id], Point(0, 0), Point(rect.w, rect.h));
	std::shared_ptr<IImage> img = GH.renderHandler().createImage(canvasScaled.getInternalSurface());

	return std::make_shared<CPicture>(img, Point(rect.x, rect.y));
}

CMapOverview::CMapOverviewWidget::CMapOverviewWidget(CMapOverview& parent):
	InterfaceObjectConfigurable(), p(parent)
{
	drawPlayerElements = p.tabType == ESelectionScreen::newGame;

	const JsonNode config(JsonPath::builtin("config/widgets/mapOverview.json"));

	if(settings["lobby"]["mapPreview"].Bool())
	{
		ResourcePath res = ResourcePath(p.resource.getName(), EResType::MAP);
		std::unique_ptr<CMap> campaignMap = nullptr;
		if(p.tabType != ESelectionScreen::newGame && config["variables"]["mapPreviewForSaves"].Bool())
		{
			CLoadFile lf(*CResourceHandler::get()->getResourceName(ResourcePath(p.resource.getName(), EResType::SAVEGAME)), MINIMAL_SERIALIZATION_VERSION);
			lf.checkMagicBytes(SAVEGAME_MAGIC);

			std::unique_ptr<CMapHeader> mapHeader = std::make_unique<CMapHeader>();
			StartInfo * startInfo;
			lf >> *(mapHeader) >> startInfo;

			if(startInfo->campState)
				campaignMap = startInfo->campState->getMap(*startInfo->campState->currentScenario());
			res = ResourcePath(startInfo->fileURI, EResType::MAP);
		}
		if(!campaignMap)
			minimaps = createMinimaps(res);
		else
			minimaps = createMinimaps(campaignMap);
	}

	REGISTER_BUILDER("drawMinimap", &CMapOverview::CMapOverviewWidget::buildDrawMinimap);

	build(config);

	if(auto w = widget<CTextBox>("fileName"))
	{
		w->setText(p.fileName);
	}
	if(auto w = widget<CLabel>("mapName"))
	{
		w->setText(p.mapName);
	}
	if(auto w = widget<CLabel>("date"))
	{
		if(p.date.empty())
		{
			std::time_t time = boost::filesystem::last_write_time(*CResourceHandler::get()->getResourceName(ResourcePath(p.resource.getName(), EResType::MAP)));
			w->setText(vstd::getFormattedDateTime(time));
		}
		else
			w->setText(p.date);
	}
	if(auto w = widget<CLabel>("noUnderground"))
	{
		if(minimaps.size() == 0)
			w->setText("");
	}
}

<?php
/**
 * This file is part of OpenMediaVault.
 *
 * @license   http://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    Volker Theile <volker.theile@openmediavault.org>
 * @copyright Copyright (c) 2009-2019 Volker Theile
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenMediaVault. If not, see <http://www.gnu.org/licenses/>.
 */
class OMVModuleQuota extends \OMV\Engine\Module\ServiceAbstract
  implements \OMV\Engine\Notify\IListener {
	/**
	 * Get the module name.
	 */
	public function getName() {
		return "quota";
	}

	/**
	 * Generate the configuration.
	 */
	public function applyConfig() {
		$cmd = new \OMV\System\Process("omv-mkconf", "quota");
		$cmd->setRedirect2to1();
		$cmd->execute();
	}

	/**
	 * Delete the associated quota configuration after a filesystem has
	 * been deleted.
	 * @param type The event message type.
	 * @param path The event message path.
	 * @param object The modified configuration object.
	 */
	public function onDeleteFilesystem($type, $path, $object) {
		// Does a quota configuration exists for the deleted filesystem?
		$db = \OMV\Config\Database::getInstance();
		if (TRUE === $db->exists("conf.system.filesystem.quota", [
			"operator" => "stringEquals",
			"arg0" => "fsuuid",
			"arg1" => $object['uuid']
		])) {
			// Execute RPC to delete the configuration object.
			\OMV\Rpc\Rpc::call("Quota", "delete", array(
			  "uuid" => $qObject['uuid']),
			  $this->getAdminContext());
		}
	}

	/**
	 * Bind listeners.
	 */
	function bindListeners(\OMV\Engine\Notify\Dispatcher $dispatcher) {
		$dispatcher->addListener(
		  OMV_NOTIFY_DELETE,
		  "org.openmediavault.conf.system.filesystem",
		  [ $this, "onDeleteFilesystem" ]);
		$dispatcher->addListener(
		  OMV_NOTIFY_CREATE | OMV_NOTIFY_MODIFY,
		  "org.openmediavault.conf.system.filesystem.quota",
		  [ $this, "setDirty" ]);
	}
}
